/**
 * HydraFS Motion Lib
 *
 * 统一动效语言，零运行时依赖（不引入 framer-motion）。
 * 包含：
 *   1. transitions  — 常用 CSS transition 字符串预设
 *   2. springs      — 常用 ease/duration 组合
 *   3. useInView    — IntersectionObserver hook，做"进入视口才动"
 *   4. useScrollProgress — 监听元素相对视口的滚动进度 0~1
 *   5. useReducedMotion  — 尊重用户的"减少动效"系统设置
 */

import { useEffect, useRef, useState } from 'react';

/* ===== 1. 预设 transitions（直接拼到 styled.css） ===== */
export const transitions = {
  fast:   'all 120ms cubic-bezier(.16,1,.3,1)',
  base:   'all 200ms cubic-bezier(.16,1,.3,1)',
  slow:   'all 380ms cubic-bezier(.16,1,.3,1)',
  story:  'all 680ms cubic-bezier(.16,1,.3,1)',
  spring: 'all 320ms cubic-bezier(.34,1.56,.64,1)',
  // 单独维度（避免 all 引发不必要的重绘）
  transform: 'transform 200ms cubic-bezier(.16,1,.3,1)',
  opacity:   'opacity 200ms cubic-bezier(.16,1,.3,1)',
};

/* ===== 2. 缓动与时长 ===== */
export const ease = {
  out:    'cubic-bezier(.16,1,.3,1)',
  inOut:  'cubic-bezier(.65,0,.35,1)',
  spring: 'cubic-bezier(.34,1.56,.64,1)',
};

export const duration = {
  fast: 120, base: 200, slow: 380, story: 680,
};

/* ===== 3. useReducedMotion — 尊重系统偏好 ===== */
export const useReducedMotion = () => {
  const [reduced, setReduced] = useState(false);
  useEffect(() => {
    if (typeof window === 'undefined' || !window.matchMedia) return;
    const mq = window.matchMedia('(prefers-reduced-motion: reduce)');
    const update = () => setReduced(mq.matches);
    update();
    mq.addEventListener?.('change', update);
    return () => mq.removeEventListener?.('change', update);
  }, []);
  return reduced;
};

/* ===== 4. useInView — 进入视口触发 ===== */
/**
 * 用法：
 *   const { ref, inView } = useInView({ threshold: 0.2 });
 *   <div ref={ref} style={{
 *     opacity: inView ? 1 : 0,
 *     transform: inView ? 'translateY(0)' : 'translateY(16px)',
 *     transition: transitions.slow,
 *     transitionDelay: `${index * 60}ms`,
 *   }} />
 */
export const useInView = ({ threshold = 0.15, rootMargin = '0px', once = true } = {}) => {
  const ref = useRef(null);
  const [inView, setInView] = useState(false);
  useEffect(() => {
    const el = ref.current;
    if (!el || typeof IntersectionObserver === 'undefined') {
      setInView(true);
      return;
    }
    const io = new IntersectionObserver(
      ([entry]) => {
        if (entry.isIntersecting) {
          setInView(true);
          if (once) io.disconnect();
        } else if (!once) {
          setInView(false);
        }
      },
      { threshold, rootMargin }
    );
    io.observe(el);
    return () => io.disconnect();
  }, [threshold, rootMargin, once]);
  return { ref, inView };
};

/* ===== 5. useScrollProgress — 元素相对视口的滚动进度 ===== */
/**
 * 返回 0~1：当 ref 元素的顶部到达视口底部时为 0；
 *              当 ref 元素的底部离开视口顶部时为 1。
 * 用 rAF 节流，性能安全。
 */
export const useScrollProgress = (refExternal) => {
  const refInternal = useRef(null);
  const ref = refExternal || refInternal;
  const [progress, setProgress] = useState(0);
  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    let ticking = false;
    const compute = () => {
      const rect = el.getBoundingClientRect();
      const vh = window.innerHeight || document.documentElement.clientHeight;
      // 0 = 元素刚进入视口底部；1 = 元素刚离开视口顶部
      const total = rect.height + vh;
      const passed = vh - rect.top;
      const p = Math.min(1, Math.max(0, passed / total));
      setProgress(p);
      ticking = false;
    };
    const onScroll = () => {
      if (ticking) return;
      ticking = true;
      requestAnimationFrame(compute);
    };
    compute();
    window.addEventListener('scroll', onScroll, { passive: true });
    window.addEventListener('resize', onScroll);
    return () => {
      window.removeEventListener('scroll', onScroll);
      window.removeEventListener('resize', onScroll);
    };
  }, [ref]);
  return { ref, progress };
};

/* ===== 6. useWindowScrollY — 简单的全局滚动距离（像素） ===== */
export const useWindowScrollY = () => {
  const [y, setY] = useState(0);
  useEffect(() => {
    let ticking = false;
    const onScroll = () => {
      if (ticking) return;
      ticking = true;
      requestAnimationFrame(() => {
        setY(window.scrollY || 0);
        ticking = false;
      });
    };
    onScroll();
    window.addEventListener('scroll', onScroll, { passive: true });
    return () => window.removeEventListener('scroll', onScroll);
  }, []);
  return y;
};

/* ===== 7. 常用进场变量（手动写 style 用） ===== */
export const fadeUp = (inView, delayMs = 0) => ({
  opacity: inView ? 1 : 0,
  transform: inView ? 'translateY(0)' : 'translateY(16px)',
  transition: transitions.slow,
  transitionDelay: `${delayMs}ms`,
  willChange: 'opacity, transform',
});

export const fadeIn = (inView, delayMs = 0) => ({
  opacity: inView ? 1 : 0,
  transition: transitions.slow,
  transitionDelay: `${delayMs}ms`,
});

export const scaleIn = (inView, delayMs = 0) => ({
  opacity: inView ? 1 : 0,
  transform: inView ? 'scale(1)' : 'scale(.96)',
  transition: transitions.slow,
  transitionDelay: `${delayMs}ms`,
  willChange: 'opacity, transform',
});
