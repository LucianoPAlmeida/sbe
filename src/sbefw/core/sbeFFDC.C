/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/sbefw/core/sbeFFDC.C $                                    */
/*                                                                        */
/* OpenPOWER sbe Project                                                  */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2016,2018                        */
/* [+] International Business Machines Corp.                              */
/*                                                                        */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/*                                                                        */
/* IBM_PROLOG_END_TAG                                                     */
#include "sbefifo.H"
#include "sbetrace.H"
#include "sbe_sp_intf.H"
#include "sbeFifoMsgUtils.H"
#include "sberegaccess.H"
#include "sbeFFDC.H"
#include "sbeglobals.H"
#include "sbecmdcntrldmt.H"

void captureAsyncFFDC(uint32_t primRc, uint32_t secRc)
{
    SBE_GLOBAL->failedPrimStatus = primRc;
    SBE_GLOBAL->failedSecStatus  = secRc;

    // Transition to dump state
    (void)SbeRegAccess::theSbeRegAccess().stateTransition(
                                           SBE_DUMP_FAILURE_EVENT);

    // Set async ffdc bit
    (void)SbeRegAccess::theSbeRegAccess().updateAsyncFFDCBit(true);
}

void SbeFFDCPackage::updateUserDataHeader(uint32_t i_fieldsConfig)
{
    // Set failed command information
    iv_sbeFFDCDataHeader.primaryStatus = SBE_GLOBAL->failedPrimStatus;
    iv_sbeFFDCDataHeader.secondaryStatus = SBE_GLOBAL->failedSecStatus;
    iv_sbeFFDCHeader.setCmdInfo(SBE_GLOBAL->failedSeqId,
                                SBE_GLOBAL->failedCmdClass,
                                SBE_GLOBAL->failedCmd);

    //Update the user data header with dump fields configuration
    iv_sbeFFDCDataHeader.dumpFields.set(i_fieldsConfig);
    iv_sbeFFDCHeader.lenInWords = (sizeof(sbeResponseFfdc_t) +
                                   sizeof(sbeFFDCDataHeader_t))
                                   /sizeof(uint32_t);
    //Update the length in ffdc package header base on required fields
    for(auto &sbeFFDCUserData:sbeFFDCUserDataArray)
    {
        if(sbeFFDCUserData.userDataId.fieldId & i_fieldsConfig)
        {
            iv_sbeFFDCHeader.lenInWords +=
                                    (sbeFFDCUserData.userDataId.fieldLen +
                                     sizeof(sbeFFDCUserDataIdentifier_t))
                                     /sizeof(uint32_t);
        }
    }
}

uint32_t SbeFFDCPackage::collectAsyncHwpFfdc (void)
{
    #define SBE_FUNC "collectAsyncHwpFfdc"
    uint32_t l_rc = SBE_SEC_OPERATION_SUCCESSFUL;

    switch (SBE_GLOBAL->asyncFfdcRC)
    {
        case fapi2::RC_CHECK_MASTER_STOP15_DEADMAN_TIMEOUT:
        case fapi2::RC_CHECK_MASTER_STOP15_INVALID_STATE:
        case fapi2::RC_BLOCK_WAKEUP_INTR_CHECK_FAIL:
            SBE_INFO (SBE_FUNC "Collecting DMT Async FFDC for RC 0x%08x",
                      SBE_GLOBAL->asyncFfdcRC);
            l_rc = sbeCollectDeadmanFfdc ();
            break;
        default:
            SBE_INFO (SBE_FUNC"No specific Async FFDC to collect");
            break;
    }

    return l_rc;
    #undef SBE_FUNC
}

uint32_t SbeFFDCPackage::sendOverFIFO(const uint32_t i_fieldsConfig,
                                      uint32_t &o_bytesSent,
                                      const bool i_skipffdcBitCheck)
{
    #define SBE_FUNC "sendOverFIFO"
    SBE_ENTER(SBE_FUNC);
    uint32_t rc = SBE_SEC_OPERATION_SUCCESSFUL;
    uint32_t length = 0;

    do
    {
        //reset sent bytes
        o_bytesSent = 0;

        //check if SBE internal FFDC should be generated
        if(!i_skipffdcBitCheck &&
           (SbeRegAccess::theSbeRegAccess().isSendInternalFFDCSet() 
            == false))
        {
            SBE_INFO(SBE_FUNC" isSendInternalFFDCSet()=false, "
                    "not generating SBE InternalFFDC");
            rc = SBE_SEC_OPERATION_SUCCESSFUL;
            break;
        }

        //Update the user data header with dump fields configuration
        updateUserDataHeader(i_fieldsConfig);

        //Send FFDC package header
        length = sizeof(iv_sbeFFDCHeader) / sizeof(uint32_t);
        rc = sbeDownFifoEnq_mult(length,
                                 (uint32_t *)(&(iv_sbeFFDCHeader)));
        if( rc!= SBE_SEC_OPERATION_SUCCESSFUL)
        {
            break;
        }
        o_bytesSent += length;

        //Send FFDC user data header
        length = sizeof(iv_sbeFFDCDataHeader) / sizeof(uint32_t);
        rc = sbeDownFifoEnq_mult(length,
                                 (uint32_t *)(&(iv_sbeFFDCDataHeader)));
        if( rc!= SBE_SEC_OPERATION_SUCCESSFUL)
        {
            break;
        }
        o_bytesSent += length;

        //Send FFDC user data blobs
        for(auto &sbeFFDCUserData:sbeFFDCUserDataArray)
        {
            if(sbeFFDCUserData.userDataId.fieldId & i_fieldsConfig)
            {
                //Send User data identifer and length
                length = sizeof(sbeFFDCUserDataIdentifier_t) / sizeof(uint32_t);
                rc = sbeDownFifoEnq_mult(length,
                                    (uint32_t*)&(sbeFFDCUserData.userDataId));
                if( rc!= SBE_SEC_OPERATION_SUCCESSFUL)
                {
                    break;
                }
                o_bytesSent += length;

                //Send User data
                length = sbeFFDCUserData.userDataId.fieldLen / sizeof(uint32_t);
                rc = sbeDownFifoEnq_mult(length,
                                        (uint32_t*)sbeFFDCUserData.userDataPtr);
                if( rc!= SBE_SEC_OPERATION_SUCCESSFUL)
                {
                    break;
                }
                o_bytesSent += length;
            }
        }

        SBE_INFO(SBE_FUNC "Number of words sent [%d]", o_bytesSent);
    } while(false);

    SBE_EXIT(SBE_FUNC);
    return rc;
    #undef SBE_FUNC
}

uint32_t SbeFFDCPackage::sendOverHostIntf(const uint32_t i_fieldsConfig,
                                          sbeMemAccessInterface *i_pMemInterface,
                                          uint32_t i_allocatedSize,
                                          const bool i_skipffdcBitCheck)
{
    #define SBE_FUNC "sendOverHostIntf"
    SBE_ENTER(SBE_FUNC);
    #if HOST_INTERFACE_AVAILABLE
    uint32_t rc = SBE_SEC_OPERATION_SUCCESSFUL;
    uint32_t length = 0;
    bool isLastAccess = false;
    fapi2::ReturnCode fapiRc = fapi2::FAPI2_RC_SUCCESS;

    do
    {
        //check if SBE internal FFDC should be generated
        if(!i_skipffdcBitCheck &&
           (SbeRegAccess::theSbeRegAccess().isSendInternalFFDCSet()
            == false))
        {
            SBE_INFO(SBE_FUNC" isSendInternalFFDCSet()=false, "
                    "not generating SBE InternalFFDC");
            rc = SBE_SEC_OPERATION_SUCCESSFUL;
            break;
        }

        //Update the user data header with dump fields configuration
        updateUserDataHeader(i_fieldsConfig);

        //Send FFDC package header
        length = sizeof(iv_sbeFFDCHeader);
        MEM_AVAILABLE_CHECK(i_allocatedSize, length, isLastAccess);
        fapiRc = i_pMemInterface->accessWithBuffer(&iv_sbeFFDCHeader, length,
                                                    isLastAccess);
        if(fapiRc != fapi2::FAPI2_RC_SUCCESS)
        {
            rc = SBE_SEC_GENERIC_FAILURE_IN_EXECUTION;
            break;
        }

        //Send FFDC user data header
        length = sizeof(iv_sbeFFDCDataHeader);
        MEM_AVAILABLE_CHECK(i_allocatedSize, length, isLastAccess);
        fapiRc = i_pMemInterface->accessWithBuffer(&iv_sbeFFDCDataHeader,
                                                   length,
                                                   isLastAccess);
        if(fapiRc != fapi2::FAPI2_RC_SUCCESS)
        {
            rc = SBE_SEC_GENERIC_FAILURE_IN_EXECUTION;
            break;
        }

        //Send FFDC user data blobs
        for(auto &sbeFFDCUserData:sbeFFDCUserDataArray)
        {
            if(sbeFFDCUserData.userDataId.fieldId & i_fieldsConfig)
            {
                //Send User data identifer and length
                length = sizeof(sbeFFDCUserDataIdentifier_t);
                MEM_AVAILABLE_CHECK(i_allocatedSize, length, isLastAccess);
                fapiRc = i_pMemInterface->accessWithBuffer(
                                            &sbeFFDCUserData.userDataId,
                                            length,
                                            isLastAccess);
                if(fapiRc != fapi2::FAPI2_RC_SUCCESS)
                {
                    rc = SBE_SEC_GENERIC_FAILURE_IN_EXECUTION;
                    break;
                }

                //Send User data
                length = sbeFFDCUserData.userDataId.fieldLen;
                MEM_AVAILABLE_CHECK(i_allocatedSize, length, isLastAccess);
                isLastAccess = isLastAccess ||
                                 (&sbeFFDCUserData ==
                                    &sbeFFDCUserDataArray[NUM_USER_DATA_ELE-1]);
                fapiRc = i_pMemInterface->accessWithBuffer(
                                            sbeFFDCUserData.userDataPtr,
                                            length,
                                            isLastAccess);
                if(fapiRc != fapi2::FAPI2_RC_SUCCESS)
                {
                    rc = SBE_SEC_GENERIC_FAILURE_IN_EXECUTION;
                    break;
                }
            }
        }
    } while(false);
    SBE_INFO(SBE_FUNC" [%d] bytes sent",
                            SBE_GLOBAL->hostFFDCAddr.size - i_allocatedSize);

    #else  //HOST_INTERFACE_AVAILABLE
    uint32_t rc = SBE_SEC_COMMAND_NOT_SUPPORTED;
    #endif //HOST_INTERFACE_AVAILABLE
    SBE_EXIT(SBE_FUNC);
    return rc;
    #undef SBE_FUNC
}
