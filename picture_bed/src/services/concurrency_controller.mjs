const clamp = (value, min, max) => Math.max(min, Math.min(max, value));

export const median = values => {
  if (!values.length) return null;
  const sorted = [...values].sort((a, b) => a - b);
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
};

export const parseRetryAfterMs = (value, now = Date.now()) => {
  if (value === null || value === undefined || value === '') return null;
  const seconds = Number(value);
  if (Number.isFinite(seconds)) return Math.max(0, seconds * 1000);
  const date = Date.parse(String(value));
  return Number.isFinite(date) ? Math.max(0, date - now) : null;
};

export const retryDelayMs = ({ attempt, retryAfter, now = Date.now(), random = Math.random, baseMs = 200 }) => {
  const serverDelay = parseRetryAfterMs(retryAfter, now);
  const base = serverDelay === null ? Math.min(8000, Math.max(0, baseMs) * (2 ** attempt)) : Math.min(30000, serverDelay);
  const jitter = 0.8 + Math.max(0, Math.min(1, random())) * 0.4;
  return Math.min(30000, Math.round(base * jitter));
};

export class AdaptiveConcurrencyController {
  constructor(config = {}) {
    this.mode = config.mode || 'adaptive';
    this.min = Math.max(1, Number(config.min ?? 4));
    this.max = Math.max(this.min, Number(config.max ?? 16));
    this.initial = clamp(Number(config.initial ?? 8), this.min, this.max);
    this.cwnd = this.initial;
    this.samples = [];
    this.baselineSamples = [];
    this.baseline = null;
    this.baselineJustEstablished = false;
    this.srtt = null;
    this.cooldownSuccesses = 0;
    this.congestionEvents = 0;
    this.congestionLatched = false;
    this.alpha = Number(config.ewmaAlpha ?? 0.125);
    this.warmupSamples = Math.max(8, Number(config.warmupSamples ?? 8));
  }

  get size() {
    return this.mode === 'fixed' ? Math.floor(this.initial) : Math.max(this.min, Math.floor(this.cwnd));
  }

  get state() {
    const baseline = this.baseline || this.srtt;
    const ratio = baseline && this.srtt ? this.srtt / baseline : null;
    return {
      cwnd: this.cwnd,
      size: this.size,
      srtt: this.srtt,
      baseline,
      baselineSamples: this.baselineSamples.length,
      phase: this.congestionLatched ? 'congested' : ratio === null ? 'warmup' : ratio >= 2.5 ? 'congested' : ratio >= 1.8 ? 'degraded' : 'healthy',
      cooldownSuccesses: this.cooldownSuccesses,
      congestionEvents: this.congestionEvents,
    };
  }

  record({ success, timeout = false, status = 0, rtt = 0 }) {
    const sample = { success: Boolean(success), timeout: Boolean(timeout), status: Number(status) || 0, rtt: Math.max(0, Number(rtt) || 0) };
    this.samples.push(sample);
    if (this.samples.length > 16) this.samples.shift();
    if (sample.success && sample.rtt > 0) {
      this.srtt = this.srtt === null ? sample.rtt : this.alpha * sample.rtt + (1 - this.alpha) * this.srtt;
      if (this.baselineSamples.length < this.warmupSamples) {
        this.baselineSamples.push(sample.rtt);
        if (this.baselineSamples.length === this.warmupSamples) {
          this.baseline = median(this.baselineSamples);
          this.baselineJustEstablished = true;
        }
      }
    }

    if (this.mode === 'fixed') return this.state;
    const recent = this.samples;
    const failureRate = recent.filter(item => !item.success).length / recent.length;
    const timeoutRate = recent.filter(item => item.timeout).length / recent.length;
    const baseline = this.baseline || this.srtt;
    const rttRatio = baseline && this.srtt ? this.srtt / baseline : 0;
    const explicitCongestion = !sample.success || sample.timeout || sample.status === 429 || sample.status === 503;
    const congestion = explicitCongestion || failureRate >= 0.2 || timeoutRate >= 0.1 || rttRatio >= 2.5;

    if (this.baselineJustEstablished && !congestion) {
      this.baselineJustEstablished = false;
      return this.state;
    }

    if (!this.baseline) return this.state;

    if (congestion) {
      if (!this.congestionLatched) {
        this.cwnd = clamp(this.cwnd * 0.5, this.min, this.max);
        this.congestionEvents++;
        this.congestionLatched = true;
      }
      this.cooldownSuccesses = 0;
      return this.state;
    }
    if (sample.success) {
      this.cooldownSuccesses++;
      const degraded = rttRatio >= 1.8;
      if (this.congestionLatched && !degraded && failureRate < 0.2 && timeoutRate < 0.1 &&
          this.cooldownSuccesses < Math.max(4, Math.floor(this.cwnd))) {
        return this.state;
      }
      if (!degraded && failureRate < 0.2 && timeoutRate < 0.1) {
        this.congestionLatched = false;
      }
      const needed = Math.max(4, Math.floor(this.cwnd));
      if (!degraded && this.cooldownSuccesses >= needed) {
        this.cwnd = clamp(this.cwnd + (1 / Math.max(this.cwnd, 1)), this.min, this.max);
        this.cooldownSuccesses = 0;
      }
    }
    return this.state;
  }
}
