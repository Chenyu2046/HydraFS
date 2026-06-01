import React, { Suspense, lazy } from 'react';
import { BrowserRouter as Router, Routes, Route, Navigate, useLocation } from 'react-router-dom';
import { Spin } from 'antd';
import Login from './pages/Login';
import Home from './pages/Home';
import FileList from './pages/FileList';
import WikiDetail from './pages/WikiDetail';
import AppShell from './components/AppShell';
import { AuthProvider, useAuth } from './contexts/AuthContext';

const Knowledge = lazy(() => import('./pages/Knowledge'));
const Graph = lazy(() => import('./pages/Graph'));
const SharedHub = lazy(() => import('./pages/SharedHub'));

const PrivateRoute = ({ children }) => {
  const { user } = useAuth();
  return user ? children : <Navigate to="/login" replace />;
};

const CRUMBS = {
  '/': ['AI 搜索'],
  '/files': ['文件管理'],
  '/knowledge': ['AI Wiki'],
  '/graph': ['知识图谱'],
  '/shared': ['分享管理'],
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
  return (
    <Routes>
      <Route path="/login" element={!user ? <Login /> : <Navigate to="/" replace />} />
      <Route path="/" element={<PrivateRoute><Shell><Home /></Shell></PrivateRoute>} />
      <Route path="/files" element={<PrivateRoute><Shell><FileList /></Shell></PrivateRoute>} />
      <Route path="/images" element={<Navigate to="/files?type=image" replace />} />
      <Route path="/knowledge" element={<PrivateRoute><Shell><Suspense fallback={<Fallback />}><Knowledge /></Suspense></Shell></PrivateRoute>} />
      <Route path="/graph" element={<PrivateRoute><Shell><Suspense fallback={<Fallback />}><Graph /></Suspense></Shell></PrivateRoute>} />
      <Route path="/shared" element={<PrivateRoute><Shell><Suspense fallback={<Fallback />}><SharedHub /></Suspense></Shell></PrivateRoute>} />
      <Route path="/top-downloads" element={<Navigate to="/shared?tab=top" replace />} />
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
