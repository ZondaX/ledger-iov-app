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

#if defined(TARGET_NANOX)
// For some reason NanoX requires this function
void __assert_fail(const char * assertion, const char * file, unsigned int line, const char * function){
    while(1) {};
}
#endif

#ifdef MAINNET_ENABLED
#define FIELD_TOTAL_FIXCOUNT 5

#define FIELD_CHAINID -100
#define FIELD_SOURCE 0
#define FIELD_DESTINATION 1
#define FIELD_AMOUNT 2
#define FIELD_FEE 3
#define FIELD_MEMO 4
#else
#define FIELD_TOTAL_FIXCOUNT 6

#define FIELD_CHAINID 0
#define FIELD_SOURCE 1
#define FIELD_DESTINATION 2
#define FIELD_AMOUNT 3
#define FIELD_FEE 4
#define FIELD_MEMO 5
#endif

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
    uint8_t fields = FIELD_TOTAL_FIXCOUNT;
    fields += parser_tx_obj.multisig.count;
    if (parser_tx_obj.sendmsg.memoLen == 0)
        fields--;
    return fields;
}

uint8_t UI_buffer[UI_BUFFER];

int8_t parser_mapDisplayIdx(const parser_context_t *ctx, int8_t displayIdx) {
    if (parser_tx_obj.sendmsg.memoLen == 0 && displayIdx >= FIELD_MEMO) {
        // SKIP Memo Field
        return displayIdx + 1;
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

    parser_error_t err = parser_ok;
    *pageCount = 1;
    switch (parser_mapDisplayIdx(ctx, displayIdx)) {
        case FIELD_CHAINID:     // ChainID
            snprintf(outKey, outKeyLen, "ChainID");
            parser_arrayToString(outValue, outValueLen,
                                 parser_tx_obj.chainID, parser_tx_obj.chainIDLen,
                                 pageIdx, pageCount);
            break;
        case FIELD_SOURCE:     // Source
            snprintf(outKey, outKeyLen, "Source");
            err = parser_getAddress(parser_tx_obj.chainID, parser_tx_obj.chainIDLen,
                                    (char *) UI_buffer, UI_BUFFER,
                                    parser_tx_obj.sendmsg.sourcePtr,
                                    parser_tx_obj.sendmsg.sourceLen);
            // page it
            parser_arrayToString(outValue, outValueLen, UI_buffer,
                                 strlen((char *) UI_buffer), pageIdx, pageCount);
            break;
        case FIELD_DESTINATION:     // Destination
            snprintf(outKey, outKeyLen, "Dest");
            err = parser_getAddress(parser_tx_obj.chainID, parser_tx_obj.chainIDLen,
                                    (char *) UI_buffer, UI_BUFFER,
                                    parser_tx_obj.sendmsg.destinationPtr,
                                    parser_tx_obj.sendmsg.destinationLen);
            // page it
            parser_arrayToString(outValue, outValueLen, UI_buffer,
                                 strlen((char *) UI_buffer), pageIdx, pageCount);
            break;
        case FIELD_AMOUNT: {
            char ticker[IOV_TICKER_MAXLEN];
            err = parser_arrayToString(ticker, IOV_TICKER_MAXLEN,
                                       parser_tx_obj.sendmsg.amount.tickerPtr,
                                       parser_tx_obj.sendmsg.amount.tickerLen,
                                       0, NULL);
            if (err != parser_ok)
                return err;

            snprintf(outKey, outKeyLen, "Amount [%s]", ticker);
            err = parser_formatAmountFriendly(outValue,
                                              outValueLen,
                                              &parser_tx_obj.sendmsg.amount);
            break;
        }
        case FIELD_FEE: {
            char ticker[IOV_TICKER_MAXLEN];
            err = parser_arrayToString(ticker, IOV_TICKER_MAXLEN,
                                       parser_tx_obj.fees.coin.tickerPtr,
                                       parser_tx_obj.fees.coin.tickerLen,
                                       0, NULL);
            if (err != parser_ok)
                return err;

            snprintf(outKey, outKeyLen, "Fees [%s]", ticker);
            err = parser_formatAmountFriendly(outValue,
                                              outValueLen,
                                              &parser_tx_obj.fees.coin);
            break;
        }
        case FIELD_MEMO:     // Memo
            snprintf(outKey, outKeyLen, "Memo");
            err = parser_arrayToString((char *) UI_buffer, UI_BUFFER,
                                       parser_tx_obj.sendmsg.memoPtr,
                                       parser_tx_obj.sendmsg.memoLen,
                                       0, NULL);
            asciify((char *) UI_buffer);
            // page it
            parser_arrayToString(outValue, outValueLen, UI_buffer,
                                 strlen((char *) UI_buffer),
                                 pageIdx, pageCount);
            break;
        default:
            // Handle variable fields
            if (displayIdx >= parser_getNumItems(ctx)) {
                *pageCount = 0;
                return parser_no_data;
            }

            // Map variable field to multisig
            uint8_t multisigIdx = displayIdx - FIELD_TOTAL_FIXCOUNT;
            snprintf(outKey, outKeyLen, "Multisig");
            if (parser_tx_obj.multisig.count > 1) {
                snprintf(outKey, outKeyLen, "Multisig [%d/%d]", multisigIdx + 1, parser_tx_obj.multisig.count);
            }

            uint64_to_str(outValue, outValueLen, parser_tx_obj.multisig.values[multisigIdx]);
    }

    return err;
}
