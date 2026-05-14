import { BrowserRouter as Router, Routes, Route, Navigate } from 'react-router-dom';
import { Layout } from 'antd';
import styled from '@emotion/styled';
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

const StyledLayout = styled(Layout)`
  min-height: 100vh;
  background: #F8FAFC;
`;

const PageContent = styled(Content)`
  max-width: 1280px;
  margin: 0 auto;
  padding: 24px 32px;
`;

const PrivateRoute = ({ children }) => {
  const { user } = useAuth();
  return user ? children : <Navigate to="/login" replace />;
};

function AppRoutes() {
  const { user } = useAuth();

  return (
    <StyledLayout>
      {user && <NavBar />}
      <PageContent>
        <Routes>
          <Route path="/login" element={user ? <Navigate to="/" replace /> : <Login />} />
          <Route path="/" element={<PrivateRoute><Home /></PrivateRoute>} />
          <Route path="/images" element={<PrivateRoute><ImageList /></PrivateRoute>} />
          <Route path="/files" element={<PrivateRoute><FileList /></PrivateRoute>} />
          <Route path="/shared" element={<PrivateRoute><SharedFiles /></PrivateRoute>} />
          <Route path="/top-downloads" element={<PrivateRoute><TopDownloads /></PrivateRoute>} />
          <Route path="/wiki/:md5" element={<PrivateRoute><WikiDetail /></PrivateRoute>} />
        </Routes>
      </PageContent>
    </StyledLayout>
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
