/*******************************************************************************
*   (c) 2019 ZondaX GmbH
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
********************************************************************************/

#include <stdio.h>
#include <zxmacros.h>
#include "parser.h"
#include "coin.h"
#include "bignum.h"

#if defined(TARGET_NANOX)
// For some reason NanoX requires this function
void __assert_fail(const char * assertion, const char * file, unsigned int line, const char * function){
    while(1) {};
}
#endif

#ifdef MAINNET_ENABLED
#define OFFSET 1
#define FIELD_CHAINID -100
#else
#define OFFSET 0
#define FIELD_CHAINID 0
#endif

#define FIELD_TOTAL_FIXCOUNT_SENDMSG       (6 - OFFSET)
#define FIELD_TOTAL_FIXCOUNT_VOTEMSG       (4 - OFFSET)
#define FIELD_TOTAL_FIXCOUNT_UPDATEMSG     (5 - OFFSET)
#define FIELD_TOTAL_FIXCOUNT_PARTICIPANTMSG 2

#define FIELD_INVALID      (-100)

//Fields for TxSend
#define FIELD_SOURCE      (1 - OFFSET)
#define FIELD_DESTINATION (2 - OFFSET)
#define FIELD_AMOUNT      (3 - OFFSET)
#define FIELD_FEE         (4 - OFFSET)
#define FIELD_MEMO        (5 - OFFSET)
//Fields for TxVote
#define FIELD_PROPOSAL_ID (1 - OFFSET)
#define FIELD_VOTER       (2 - OFFSET)
#define FIELD_SELECTION   (3 - OFFSET)
//Fields for TxUpdate
#define FIELD_CONTRACT_ID   (1 - OFFSET)
#define FIELD_PARTICIPANT   (2 - OFFSET)
#define FIELD_ACTIVATION_TH (3 - OFFSET)
#define FIELD_ADMIN_TH      (4 - OFFSET)
//Fields for MsgParticipant
#define FIELD_PARTICIPANT_ADDRESS 0
#define FIELD_PARTICIPANT_WEIGHT  1

// * optional chainid for testnet mode
// 0  source
// 1  destination
// 2  amount  value / ticker
// 3  fees  value / ticker
// 4  memo                      (when exists)

#define UI_BUFFER 256

parser_error_t parser_parse(parser_context_t *ctx,
                            const uint8_t *data,
                            uint16_t dataLen) {
    parser_init(ctx, data, dataLen);
    return parser_Tx(ctx);
}

parser_error_t parser_validate(const parser_context_t *ctx, bool_t isMainnet) {
    if (isMainnet != parser_IsMainnet(parser_tx_obj.chainID, parser_tx_obj.chainIDLen)) {
        return parser_unexpected_chain;
    }

    if (parser_tx_obj.sendmsg.memoLen > TX_MEMOLEN_MAX) {
        return parser_unexpected_buffer_end;
    }

    return parser_ok;
}

uint8_t parser_getNumItems(const parser_context_t *ctx) {

    uint8_t fields = 0;
    switch (parser_tx_obj.msgType) {
        case Msg_Send:
            fields = FIELD_TOTAL_FIXCOUNT_SENDMSG;
            if (parser_tx_obj.sendmsg.memoLen == 0)
                fields--;
            fields += parser_tx_obj.multisig.count;
            break;
        case Msg_Vote:
            fields = FIELD_TOTAL_FIXCOUNT_VOTEMSG;
            break;
        case Msg_Update:
            fields = FIELD_TOTAL_FIXCOUNT_UPDATEMSG - 1;
            fields += parser_tx_obj.updatemsg.participantsCount * FIELD_TOTAL_FIXCOUNT_PARTICIPANTMSG;
            break;
        default:
            return fields;
    }
    return fields;
}

uint8_t UI_buffer[UI_BUFFER];

int8_t parser_mapDisplayIdx(const parser_context_t *ctx, int8_t displayIdx) {
    switch (parser_tx_obj.msgType) {
        case Msg_Update: {
            const uint8_t numItems = parser_tx_obj.updatemsg.participantsCount * FIELD_TOTAL_FIXCOUNT_PARTICIPANTMSG;

            if (displayIdx < FIELD_PARTICIPANT) {
                return displayIdx;
            }

            if (displayIdx < (FIELD_PARTICIPANT + numItems)) {
                return FIELD_PARTICIPANT;
            }

            if (displayIdx < FIELD_TOTAL_FIXCOUNT_UPDATEMSG + numItems) {
                return displayIdx - numItems + 1;
            }

            return (uint8_t) FIELD_INVALID;
        }
        case Msg_Send: {
            if (displayIdx < FIELD_MEMO) {
                return displayIdx;
            }

            if (parser_tx_obj.sendmsg.memoLen == 0) {
                // SKIP Memo Field
                return displayIdx + 1;
            }
        }
        case Msg_Vote:
            // No changes
            break;
        default:
            return FIELD_INVALID;
    }

    return displayIdx;
}

parser_error_t parser_getItem(const parser_context_t *ctx,
                              int8_t displayIdx,
                              char *outKey, uint16_t outKeyLen,
                              char *outValue, uint16_t outValueLen,
                              uint8_t pageIdx, uint8_t *pageCount) {

    snprintf(outKey, outKeyLen, "?");
    snprintf(outValue, outValueLen, "?");

    MEMSET(UI_buffer, 0, UI_BUFFER);

    *pageCount = 1;

    switch (parser_tx_obj.msgType) {
        case Msg_Send:
            return parser_getItem_Send(ctx, displayIdx, outKey, outKeyLen,
                                       outValue, outValueLen, pageIdx, pageCount);
        case Msg_Vote:
            return parser_getItem_Vote(ctx, displayIdx, outKey, outKeyLen,
                                       outValue, outValueLen, pageIdx, pageCount);
        case Msg_Update:
            return parser_getItem_Update(ctx, displayIdx, outKey, outKeyLen,
                                         outValue, outValueLen, pageIdx, pageCount);
        case Msg_Invalid:
            return parser_unexpected_type;
    }

    return parser_unexpected_type;
}

__Z_INLINE parser_error_t parser_getItem_Participant(const parser_context_t *ctx, int8_t displayIdx,
                                                     char *outKey, uint16_t outKeyLen,
                                                     char *outValue, uint16_t outValueLen,
                                                     uint8_t pageIdx, uint8_t *pageCount) {
    *pageCount = 1;
    if (parser_tx_obj.updatemsg.participantsCount == 0) {
        return parser_no_data;
    }

    //Get on which participant index we are right now
    const uint8_t participantIdx = (displayIdx - FIELD_PARTICIPANT) / FIELD_TOTAL_FIXCOUNT_PARTICIPANTMSG;
    //Get Participants field index
    const uint8_t fieldIdx = (displayIdx - FIELD_PARTICIPANT) % FIELD_TOTAL_FIXCOUNT_PARTICIPANTMSG;

    if (participantIdx >= parser_tx_obj.updatemsg.participantsCount) {
        return parser_unexpected_field;
    }

    //Parse Participant that corresponds to participantIdx
    const parser_participant_t *p = &parser_tx_obj.updatemsg.participant_array[participantIdx];

    switch (fieldIdx) {
        case FIELD_PARTICIPANT_ADDRESS: {
            FAIL_ON_ERROR(parser_getAddress(parser_tx_obj.chainID, parser_tx_obj.chainIDLen,
                                            (char *) UI_buffer, UI_BUFFER,
                                            p->signaturePtr, p->signatureLen))
            // page it
            snprintf(outKey, outKeyLen, "Participant [%d/%d] Signature",
                     participantIdx + 1, parser_tx_obj.updatemsg.participantsCount);
            parser_arrayToString(outValue, outValueLen, UI_buffer, strlen((char *) UI_buffer), pageIdx, pageCount);
            break;
        }
        case FIELD_PARTICIPANT_WEIGHT:
            snprintf(outKey, outKeyLen, "Participant [%d/%d] Weight",
                     participantIdx + 1, parser_tx_obj.updatemsg.participantsCount);
            int64_to_str(outValue, outValueLen, p->weight);
            break;
        default:
            return parser_unexpected_field;
    }

    return parser_ok;
}

parser_error_t
parser_getItem_Send(const parser_context_t *ctx, int8_t displayIdx,
                    char *outKey, uint16_t outKeyLen,
                    char *outValue, uint16_t outValueLen,
                    uint8_t pageIdx, uint8_t *pageCount) {

    switch (parser_mapDisplayIdx(ctx, displayIdx)) {
        case FIELD_CHAINID:     // ChainID
            snprintf(outKey, outKeyLen, "ChainID");
            FAIL_ON_ERROR(parser_arrayToString(outValue, outValueLen,
                                               parser_tx_obj.chainID, parser_tx_obj.chainIDLen,
                                               pageIdx, pageCount))
            break;
        case FIELD_SOURCE:     // Source
            snprintf(outKey, outKeyLen, "Source");
            FAIL_ON_ERROR(parser_getAddress(parser_tx_obj.chainID, parser_tx_obj.chainIDLen,
                                            (char *) UI_buffer, UI_BUFFER,
                                            parser_tx_obj.sendmsg.sourcePtr,
                                            parser_tx_obj.sendmsg.sourceLen))
            // page it
            parser_arrayToString(outValue, outValueLen, UI_buffer,
                                 strlen((char *) UI_buffer), pageIdx, pageCount);
            break;
        case FIELD_DESTINATION:     // Destination
            snprintf(outKey, outKeyLen, "Dest");
            FAIL_ON_ERROR(parser_getAddress(parser_tx_obj.chainID, parser_tx_obj.chainIDLen,
                                            (char *) UI_buffer, UI_BUFFER,
                                            parser_tx_obj.sendmsg.destinationPtr,
                                            parser_tx_obj.sendmsg.destinationLen))
            // page it
            parser_arrayToString(outValue, outValueLen, UI_buffer,
                                 strlen((char *) UI_buffer), pageIdx, pageCount);
            break;
        case FIELD_AMOUNT: {
            char ticker[IOV_TICKER_MAXLEN];
            FAIL_ON_ERROR(parser_arrayToString(ticker, IOV_TICKER_MAXLEN,
                                               parser_tx_obj.sendmsg.amount.tickerPtr,
                                               parser_tx_obj.sendmsg.amount.tickerLen,
                                               0, NULL))

            snprintf(outKey, outKeyLen, "Amount [%s]", ticker);
            FAIL_ON_ERROR(parser_formatAmountFriendly(outValue,
                                                      outValueLen,
                                                      &parser_tx_obj.sendmsg.amount))
            break;
        }
        case FIELD_FEE: {
            char ticker[IOV_TICKER_MAXLEN];
            FAIL_ON_ERROR(parser_arrayToString(ticker, IOV_TICKER_MAXLEN,
                                               parser_tx_obj.fees.coin.tickerPtr,
                                               parser_tx_obj.fees.coin.tickerLen,
                                               0, NULL))

            snprintf(outKey, outKeyLen, "Fees [%s]", ticker);
            FAIL_ON_ERROR(parser_formatAmountFriendly(outValue,
                                                      outValueLen,
                                                      &parser_tx_obj.fees.coin))
            break;
        }
        case FIELD_MEMO: {     // Memo
            snprintf(outKey, outKeyLen, "Memo");
            FAIL_ON_ERROR(parser_arrayToString((char *) UI_buffer, UI_BUFFER,
                                               parser_tx_obj.sendmsg.memoPtr,
                                               parser_tx_obj.sendmsg.memoLen,
                                               0, NULL))
            asciify((char *) UI_buffer);
            // page it
            parser_arrayToString(outValue, outValueLen, UI_buffer,
                                 strlen((char *) UI_buffer),
                                 pageIdx, pageCount);
            break;
        }
        default: {
            // Handle variable fields
            if (displayIdx >= parser_getNumItems(ctx)) {
                *pageCount = 0;
                return parser_no_data;
            }

            // Map variable field to multisig
            uint8_t multisigIdx = displayIdx - FIELD_TOTAL_FIXCOUNT_SENDMSG;
            snprintf(outKey, outKeyLen, "Multisig");
            if (parser_tx_obj.multisig.count > 1) {
                snprintf(outKey, outKeyLen, "Multisig [%d/%d]", multisigIdx + 1, parser_tx_obj.multisig.count);
            }

            uint64_to_str(outValue, outValueLen, parser_tx_obj.multisig.values[multisigIdx]);
        }
    }
    return parser_ok;
}

__Z_INLINE parser_error_t parser_getItem_Vote(const parser_context_t *ctx, int8_t displayIdx,
                                              char *outKey, uint16_t outKeyLen,
                                              char *outValue, uint16_t outValueLen,
                                              uint8_t pageIdx, uint8_t *pageCount) {
    parser_error_t err = parser_ok;

    switch (parser_mapDisplayIdx(ctx, displayIdx)) {
        case FIELD_CHAINID:     // ChainID
            snprintf(outKey, outKeyLen, "ChainID");
            FAIL_ON_ERROR(parser_arrayToString(outValue, outValueLen,
                                               parser_tx_obj.chainID,
                                               parser_tx_obj.chainIDLen,
                                               pageIdx, pageCount))
            break;
        case FIELD_VOTER: {     // Voter
            snprintf(outKey, outKeyLen, "Voter");
            FAIL_ON_ERROR(parser_getAddress(parser_tx_obj.chainID, parser_tx_obj.chainIDLen,
                                            (char *) UI_buffer, UI_BUFFER,
                                            parser_tx_obj.votemsg.voterPtr,
                                            parser_tx_obj.votemsg.voterLen))
            // page it
            parser_arrayToString(outValue, outValueLen, UI_buffer,
                                 strlen((char *) UI_buffer), pageIdx, pageCount);
            break;
        }
        case FIELD_PROPOSAL_ID: { //Proposal Id
            snprintf(outKey, outKeyLen, "ProposalId");
            uint8_t bcdOut[20]; //Must be  at most outValueLen/2
            uint16_t bcdOutLen = sizeof(bcdOut);
            bignumBigEndian_to_bcd(bcdOut, bcdOutLen, parser_tx_obj.votemsg.proposalIdPtr,
                                   parser_tx_obj.votemsg.proposalIdLen);
            if (!bignumBigEndian_bcdprint(outValue, outValueLen, bcdOut, bcdOutLen)) {
                return parser_unexpected_buffer_end;
            }
            break;
        }
        case FIELD_SELECTION: { // Vote option
            const char *sel;
            switch (parser_tx_obj.votemsg.voteOption) {
                case VOTE_OPTION_YES:
                    sel = VOTE_OPTION_YES_STR;
                    break;
                case VOTE_OPTION_NO:
                    sel = VOTE_OPTION_NO_STR;
                    break;
                case VOTE_OPTION_ABSTAIN:
                    sel = VOTE_OPTION_ABSTAIN_STR;
                    break;
                default:
                    sel = VOTE_OPTION_INVALID_STR;
            }
            snprintf(outKey, outKeyLen, "Selection");
            snprintf(outValue, outValueLen, "%s", sel);
            break;
        }
        default: {
            // Handle variable fields
            if (displayIdx >= parser_getNumItems(ctx)) {
                *pageCount = 0;
                return parser_no_data;
            }
        }
    }

    return err;
}

__Z_INLINE parser_error_t parser_getItem_Update(const parser_context_t *ctx, int8_t displayIdx,
                                                char *outKey, uint16_t outKeyLen,
                                                char *outValue, uint16_t outValueLen,
                                                uint8_t pageIdx, uint8_t *pageCount) {

    switch (parser_mapDisplayIdx(ctx, displayIdx)) {
        case FIELD_CHAINID:     // ChainID
            snprintf(outKey, outKeyLen, "ChainID");
            return parser_arrayToString(outValue, outValueLen,
                                        parser_tx_obj.chainID,
                                        parser_tx_obj.chainIDLen,
                                        pageIdx, pageCount);
        case FIELD_CONTRACT_ID: { //Contract Id
            snprintf(outKey, outKeyLen, "ContractId");
            uint8_t bcdOut[20]; //Must be  at most outValueLen/2
            const uint16_t bcdOutLen = sizeof(bcdOut);
            bignumBigEndian_to_bcd(bcdOut, bcdOutLen,
                                   parser_tx_obj.updatemsg.contractIdPtr,
                                   parser_tx_obj.updatemsg.contractIdLen);
            if (!bignumBigEndian_bcdprint(outValue, outValueLen, bcdOut, bcdOutLen)) {
                return parser_unexpected_buffer_end;
            }
            break;
        }
        case FIELD_PARTICIPANT:   //Participant
            return parser_getItem_Participant(ctx, displayIdx,
                                              outKey, outKeyLen,
                                              outValue, outValueLen,
                                              pageIdx, pageCount);
        case FIELD_ACTIVATION_TH:
            snprintf(outKey, outKeyLen, "ActivationTh");
            int64_to_str(outValue, outValueLen, parser_tx_obj.updatemsg.activation_th);
            break;
        case FIELD_ADMIN_TH:
            snprintf(outKey, outKeyLen, "AdminTh");
            int64_to_str(outValue, outValueLen, parser_tx_obj.updatemsg.admin_th);
            break;
        default:
            return parser_unexepected_error;
    }

    return parser_ok;
}
