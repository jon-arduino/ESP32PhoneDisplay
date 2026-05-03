#include "BackChannelParser.h"

// ─────────────────────────────────────────────────────────────────────────────
//  feed — accumulate bytes and dispatch complete frames
// ─────────────────────────────────────────────────────────────────────────────
void BackChannelParser::feed(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];

        // Sync: discard bytes until we see BC_MAGIC at start of frame
        if (_len == 0 && b != BC_MAGIC) {
            _stats.syncErrors++;
            continue;
        }

        // Accumulate — guard against buffer overrun
        if (_len < BUF_SIZE) {
            _buf[_len++] = b;
        } else {
            _stats.overruns++;
            _len = 0;
            continue;
        }

        // Need at least 3 bytes to read the length field
        if (_len < 3) continue;

        // Decode frame length from header bytes 1 and 2 (little-endian)
        uint16_t frameLen  = (uint16_t)_buf[1] | ((uint16_t)_buf[2] << 8);
        size_t   totalSize = 3 + frameLen;   // header(3) + cmd(1) + payload(N)

        // Validate: must have at least cmd byte, must fit in buffer
        if (frameLen < 1 || totalSize > BUF_SIZE) {
            _stats.invalidFrames++;
            _len = 0;
            continue;
        }

        // Wait for rest of frame to arrive
        if (_len < totalSize) continue;

        // Complete frame — dispatch and reset
        uint8_t        cmd        = _buf[3];
        const uint8_t* payload    = (frameLen > 1) ? &_buf[4] : nullptr;
        size_t         payloadLen = (frameLen > 1) ? frameLen - 1 : 0;

        dispatch(cmd, payload, payloadLen);
        _len = 0;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  dispatch — fire typed callback for each recognised command
// ─────────────────────────────────────────────────────────────────────────────
void BackChannelParser::dispatch(uint8_t cmd, const uint8_t *payload, size_t payloadLen)
{
    switch (cmd) {

        case BC_CMD_PONG:
            _stats.pong++;
            if (_pongCallback) _pongCallback();
            break;

        case BC_CMD_KEY1:
            _stats.key1++;
            if (_keyCallback) _keyCallback('1');
            break;

        case BC_CMD_KEY2:
            _stats.key2++;
            if (_keyCallback) _keyCallback('2');
            break;

        case BC_CMD_TOUCH_DOWN:
        case BC_CMD_TOUCH_MOVE: {
            if (payload && payloadLen >= BC_TOUCH_PAYLOAD_LEN) {
                int16_t x = (int16_t)((payload[1] << 8) | payload[0]);  // little-endian
                int16_t y = (int16_t)((payload[3] << 8) | payload[2]);  // little-endian
                uint8_t z = payload[4];  // BC_TOUCH_Z_CONTACT (128) from iPhone
                _stats.touch++;
                if (_touchCallback) _touchCallback(cmd, x, y, z);
            }
            break;
        }

        case BC_CMD_TOUCH_UP:
            _stats.touch++;
            if (_touchCallback) _touchCallback(BC_CMD_TOUCH_UP, 0, 0, BC_TOUCH_Z_NONE);
            break;

        default:
            _stats.unknownCmds++;
            break;
    }
}