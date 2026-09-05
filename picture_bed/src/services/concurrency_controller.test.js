import { AdaptiveConcurrencyController, parseRetryAfterMs, retryDelayMs } from './concurrency_controller.mjs';

describe('HydraStore concurrency controller', () => {
  test('uses fractional cwnd and waits for a full cooldown before growing', () => {
    const controller = new AdaptiveConcurrencyController({ initial: 8, min: 4, max: 16 });
    for (let i = 0; i < 8; i++) controller.record({ success: true, rtt: 10 });
    expect(controller.state.baseline).toBe(10);
    expect(controller.state.cwnd).toBe(8);
    for (let i = 0; i < 7; i++) controller.record({ success: true, rtt: 10 });
    expect(controller.state.cwnd).toBe(8);
    controller.record({ success: true, rtt: 10 });
    expect(controller.state.cwnd).toBeCloseTo(8.125);
  });

  test('halves on gateway backpressure and does not grow while RTT is degraded', () => {
    const controller = new AdaptiveConcurrencyController({ initial: 8, min: 4, max: 16 });
    for (let i = 0; i < 8; i++) controller.record({ success: true, rtt: 10 });
    controller.record({ success: false, status: 429, rtt: 12 });
    expect(controller.state.cwnd).toBe(4);
    for (let i = 0; i < 10; i++) controller.record({ success: true, rtt: 100 });
    expect(controller.state.cwnd).toBe(4);
    expect(controller.state.phase).toBe('congested');
  });

  test('does not repeatedly halve or reset the same congestion episode', () => {
    const controller = new AdaptiveConcurrencyController({ initial: 8, min: 1, max: 16 });
    for (let i = 0; i < 8; i++) controller.record({ success: true, rtt: 10 });
    controller.record({ success: false, status: 503, rtt: 12 });
    controller.record({ success: false, status: 503, rtt: 12 });
    expect(controller.state.cwnd).toBe(4);
    expect(controller.state.congestionEvents).toBe(1);
    for (let i = 0; i < 16; i++) controller.record({ success: true, rtt: 10 });
    expect(controller.state.congestionEvents).toBe(1);
    controller.record({ success: false, status: 503, rtt: 12 });
    expect(controller.state.congestionEvents).toBe(2);
  });

  test('keeps alternating failures in one episode until recovery is stable', () => {
    const controller = new AdaptiveConcurrencyController({ initial: 16, min: 1, max: 16 });
    for (let i = 0; i < 8; i++) controller.record({ success: true, rtt: 10 });
    controller.record({ success: false, status: 503, rtt: 10 });
    controller.record({ success: true, rtt: 10 });
    controller.record({ success: false, status: 503, rtt: 10 });
    expect(controller.state.cwnd).toBe(8);
    expect(controller.state.congestionEvents).toBe(1);
    for (let i = 0; i < 8; i++) controller.record({ success: true, rtt: 10 });
    expect(controller.state.congestionEvents).toBe(1);
    controller.record({ success: false, status: 503, rtt: 10 });
    expect(controller.state.congestionEvents).toBe(2);
  });

  test('prefers server Retry-After and otherwise applies bounded jitter', () => {
    expect(parseRetryAfterMs('2')).toBe(2000);
    expect(retryDelayMs({ attempt: 2, retryAfter: '3', random: () => 0 })).toBe(2400);
    expect(retryDelayMs({ attempt: 2, random: () => 0 })).toBe(640);
    expect(retryDelayMs({ attempt: 2, random: () => 1 })).toBe(960);
  });
});
