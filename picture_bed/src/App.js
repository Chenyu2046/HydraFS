import React, { Suspense, lazy } from 'react';
import { BrowserRouter as Router, Routes, Route, Navigate, useLocation, useNavigate } from 'react-router-dom';
import { Spin, Modal } from 'antd';
import { ExclamationCircleOutlined } from '@ant-design/icons';
import Login from './pages/Login';
import Home from './pages/Home';
import FileList from './pages/FileList';
import WikiDetail from './pages/WikiDetail';
import AppShell from './components/AppShell';
import { AuthProvider, useAuth } from './contexts/AuthContext';
import { setApiKeyInvalidListener } from './services/ai';

const Knowledge = lazy(() => import('./pages/Knowledge'));
const Graph = lazy(() => import('./pages/Graph'));
const SharedHub = lazy(() => import('./pages/SharedHub'));

const PrivateRoute = ({ children }) => {
  const { user } = useAuth();
  return user ? children : <Navigate to="/login" replace />;
};

// 面包屑：仅在 ≥2 段时由 Topbar 渲染（Topbar 内部做守卫）。
// 单段路由（Files/Knowledge/Graph/Shared）与 Sidebar 选中态重复，留空避免噪音；
// 多段如 Wiki 详情才显示导航深度。
const CRUMBS = {
  // single-segment intentionally omitted — Sidebar already conveys location
};

const Shell = ({ children }) => {
  const loc = useLocation();
  let crumbs = CRUMBS[loc.pathname];
  if (!crumbs) {
    if (loc.pathname.startsWith('/wiki/')) crumbs = ['Knowledge', 'Wiki'];
    else crumbs = [];
  }
  // Overview 首页：让 Topbar 透明、内容区贴边，给 HeroCanvas 全宽 pastel 画布
  const isOverview = loc.pathname === '/';
  return (
    <AppShell
      crumbs={crumbs}
      transparentTopbar={isOverview}
      flushContent={isOverview}
    >
      {children}
    </AppShell>
  );
};

const Fallback = () => (
  <div style={{ padding: 80, textAlign: 'center' }}><Spin /></div>
);

function AppRoutes() {
  const { user } = useAuth();
  const navigate = useNavigate();

  // 注册 API Key 无效全局弹窗（services/ai.js 在 5s 节流后调用）
  React.useEffect(() => {
    setApiKeyInvalidListener(({ msg, errCode }) => {
      Modal.confirm({
        title: 'DashScope API Key 无效',
        icon: <ExclamationCircleOutlined />,
        content: (
          <div>
            <p style={{ marginBottom: 8 }}>{msg}</p>
            {errCode && (
              <p style={{ color: '#888', fontSize: 12, fontFamily: 'monospace' }}>
                错误码：{errCode}
              </p>
            )}
            <p style={{ fontSize: 12, color: '#666' }}>
              请前往 Knowledge 页重新设置可用的阿里百炼 API Key。
            </p>
          </div>
        ),
        okText: '去设置',
        cancelText: '稍后处理',
        onOk: () => navigate('/knowledge'),
      });
    });
    return () => setApiKeyInvalidListener(null);
  }, [navigate]);

  return (
    <Routes>
      <Route path="/login" element={!user ? <Login /> : <Navigate to="/" replace />} />
      <Route path="/" element={<PrivateRoute><Shell><Home /></Shell></PrivateRoute>} />
      <Route path="/files" element={<PrivateRoute><Shell><FileList /></Shell></PrivateRoute>} />
      <Route path="/images" element={<Navigate to="/files?type=image" replace />} />
      <Route path="/knowledge" element={<PrivateRoute><Shell><Suspense fallback={<Fallback />}><Knowledge /></Suspense></Shell></PrivateRoute>} />
      <Route path="/graph" element={<PrivateRoute><Shell><Suspense fallback={<Fallback />}><Graph /></Suspense></Shell></PrivateRoute>} />
      <Route path="/shared" element={<PrivateRoute><Shell><Suspense fallback={<Fallback />}><SharedHub /></Suspense></Shell></PrivateRoute>} />
      <Route path="/top-downloads" element={<Navigate to="/shared?sort=top" replace />} />
      <Route path="/wiki/:md5" element={<PrivateRoute><Shell><WikiDetail /></Shell></PrivateRoute>} />
    </Routes>
  );
}

function App() {
  return (
    <AuthProvider>
      <Router>
        <AppRoutes />
      </Router>
    </AuthProvider>
  );
}

export default App;
