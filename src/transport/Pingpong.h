#pragma once

#include <Arduino.h>
#include <functional>

// -----------------------------------------------------------------------------
//  PingPong — WiFi connection health monitor and RTT measurement
// -----------------------------------------------------------------------------

class PingPong
{
public:
    PingPong() = default;

    void setInterval(uint32_t intervalMs) { _intervalMs = intervalMs; }
    void setTimeout (uint32_t timeoutMs)  { _timeoutMs  = timeoutMs;  }

    void onRtt      (std::function<void(uint32_t rttMs)> cb) { _onRtt       = cb; }
    void onFirstPong(std::function<void()> cb)               { _onFirstPong = cb; }

    void tick(uint32_t nowMs)
    {
        if (!_active) return;

        if (_waitingForPong) {
            uint32_t elapsed = nowMs - _pingSentAt;
            if (elapsed >= 500  && !(_logged & 1)) { _logged |= 1; Serial.printf("[Ping] Late %ums — link may be slow\n", elapsed); }
            if (elapsed >= 1500 && !(_logged & 2)) { _logged |= 2; Serial.printf("[Ping] Late %ums — WARNING\n", elapsed); }
            if (elapsed >= 6000 && !(_logged & 4)) { _logged |= 4; Serial.printf("[Ping] Late %ums — CRITICAL\n", elapsed); }
            if (elapsed >= _timeoutMs) { _timedOut = true; return; }
        }

        if (nowMs - _pingSentAt >= _intervalMs)
            _pingNeeded = true;
    }

    bool pingNeeded() const { return _pingNeeded && !_timedOut; }
    bool isTimedOut() const { return _timedOut; }

    void onPingSent()
    {
        _pingSentAt     = millis();
        _waitingForPong = true;
        _pingNeeded     = false;
        _logged         = 0;
    }

    void onPongReceived()
    {
        if (!_waitingForPong) return;
        uint32_t rtt    = millis() - _pingSentAt;
        _waitingForPong = false;
        _timedOut       = false;
        _logged         = 0;

        _lastRtt  = rtt;
        _totalRtt += rtt;
        _count++;
        if (rtt < _minRtt || _count == 1) _minRtt = rtt;
        if (rtt > _maxRtt)                _maxRtt = rtt;

        if (_count == 1 && _onFirstPong) _onFirstPong();
        if (_onRtt) _onRtt(rtt);
    }

    void onConnected()
    {
        _waitingForPong = false;
        _pingNeeded     = false;
        _timedOut       = false;
        _logged         = 0;
        _pingSentAt     = millis();
        _active         = true;
    }

    void onDisconnected()
    {
        _active         = false;
        _waitingForPong = false;
        _pingNeeded     = false;
        _timedOut       = false;
    }

    uint32_t rttLast()  const { return _lastRtt;  }
    uint32_t rttMin()   const { return _minRtt;   }
    uint32_t rttMax()   const { return _maxRtt;   }
    uint32_t rttAvg()   const { return _count ? _totalRtt / _count : 0; }
    uint32_t rttCount() const { return _count;    }

    void resetStats()
    {
        _lastRtt = _minRtt = _maxRtt = _totalRtt = _count = 0;
    }

private:
    uint32_t _intervalMs = 3000;
    uint32_t _timeoutMs  = 9000;

    bool     _active         = false;
    bool     _waitingForPong = false;
    bool     _pingNeeded     = false;
    bool     _timedOut       = false;
    uint32_t _pingSentAt     = 0;
    uint8_t  _logged         = 0;

    uint32_t _lastRtt  = 0;
    uint32_t _minRtt   = 0;
    uint32_t _maxRtt   = 0;
    uint32_t _totalRtt = 0;
    uint32_t _count    = 0;

    std::function<void(uint32_t)> _onRtt;
    std::function<void()>         _onFirstPong;
};