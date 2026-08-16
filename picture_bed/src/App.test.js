import { render, screen } from '@testing-library/react';
import App from './App';
import { ThemeProvider } from './contexts/ThemeContext';

test('renders the login page for an anonymous user', () => {
  render(<ThemeProvider><App /></ThemeProvider>);
  expect(screen.getByRole('heading', { name: '欢迎回来' })).toBeInTheDocument();
});
