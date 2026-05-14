import React from 'react';
import { BrowserRouter as Router, Routes, Route, Navigate } from 'react-router-dom';
import styled from '@emotion/styled';
import { Layout } from 'antd';
import Login from './pages/Login';
import Home from './pages/Home';
import ImageList from './pages/ImageList';
import FileList from './pages/FileList';
import SharedFiles from './pages/SharedFiles';
import TopDownloads from './pages/TopDownloads';
import WikiDetail from './pages/WikiDetail';
import NavBar from './components/NavBar';
import { AuthProvider, useAuth } from './contexts/AuthContext';

const { Content } = Layout;

const AppContainer = styled.div`
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  background: #F5F5F7;
`;

const MainContent = styled(Content)`
  flex: 1;
  width: 100%;
  max-width: 1200px;
  margin: 0 auto;
  padding: 40px 24px;
`;

const PrivateRoute = ({ children }) => {
  const { user } = useAuth();
  return user ? children : <Navigate to="/login" replace />;
};

function AppRoutes() {
  const { user } = useAuth();

  return (
    <AppContainer>
      {user && <NavBar />}
      <MainContent>
        <Routes>
          <Route path="/login" element={!user ? <Login /> : <Navigate to="/" replace />} />
          <Route path="/" element={<PrivateRoute><Home /></PrivateRoute>} />
          <Route path="/images" element={<PrivateRoute><ImageList /></PrivateRoute>} />
          <Route path="/files" element={<PrivateRoute><FileList /></PrivateRoute>} />
          <Route path="/shared" element={<PrivateRoute><SharedFiles /></PrivateRoute>} />
          <Route path="/top-downloads" element={<PrivateRoute><TopDownloads /></PrivateRoute>} />
          <Route path="/wiki/:md5" element={<PrivateRoute><WikiDetail /></PrivateRoute>} />
        </Routes>
      </MainContent>
    </AppContainer>
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
