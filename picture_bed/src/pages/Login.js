import React, { useState } from 'react';
import { Form, Input, Button, message } from 'antd';
import { UserOutlined, LockOutlined, MailOutlined, PhoneOutlined } from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';
import styled from '@emotion/styled';
import SparkMD5 from 'spark-md5';
import { useAuth } from '../contexts/AuthContext';
import { useThemeMode } from '../contexts/ThemeContext';
import { loginUser, registerUser } from '../services/auth';
import MiniGraph from '../components/MiniGraph';
import Logo from '../components/Logo';
import { MOCK_GRAPH } from '../mock/graph';
import { copy } from '../lib/copy';

const Page = styled.div`
  min-height: 100vh;
  background: ${p => p.theme.colors.bg};
  display: grid;
  grid-template-columns: 1.05fr 1fr;

  @media (max-width: 880px) { grid-template-columns: 1fr; }
`;

const Hero = styled.div`
  position: relative;
  padding: 56px 56px 40px;
  display: flex; flex-direction: column; gap: 28px;
  background:
    radial-gradient(circle at 20% 20%, ${p => p.theme.colors.accentSoft} 0%, transparent 55%),
    ${p => p.theme.colors.panel};
  border-right: 1px solid ${p => p.theme.colors.border};
  overflow: hidden;

  @media (max-width: 880px) { display: none; }
`;

const Brand = styled.div`
  display: flex; align-items: center; gap: 10px;
  font-weight: 700;
  font-size: 16px;
  letter-spacing: -0.2px;
  color: ${p => p.theme.colors.text};
  .logo {
    width: 32px; height: 32px;
    border-radius: 8px;
    background: linear-gradient(135deg, ${p => p.theme.colors.accent}, ${p => p.theme.colors.accentHover});
    display: grid; place-items: center;
    color: #fff; font-size: 14px;
  }
`;

const Headline = styled.h1`
  margin: 0;
  font-size: 36px;
  letter-spacing: -1px;
  font-weight: 700;
  line-height: 1.18;
  color: ${p => p.theme.colors.text};
  span {
    background: linear-gradient(120deg, ${p => p.theme.colors.accent}, ${p => p.theme.colors.info});
    -webkit-background-clip: text; background-clip: text;
    -webkit-text-fill-color: transparent;
  }
`;

const Sub = styled.p`
  margin: 0;
  color: ${p => p.theme.colors.text2};
  font-size: 14px;
  line-height: 1.65;
  max-width: 460px;
`;

const Capsules = styled.div`
  display: flex; flex-wrap: wrap; gap: 8px;
  margin-top: 4px;
  span {
    padding: 4px 10px;
    border-radius: 999px;
    background: ${p => p.theme.colors.panel2};
    border: 1px solid ${p => p.theme.colors.border};
    font-family: ${p => p.theme.fontFamily.mono};
    font-size: 11px;
    color: ${p => p.theme.colors.text2};
    letter-spacing: 0.4px;
  }
`;

const GraphBox = styled.div`
  margin-top: auto;
  border: 1px solid ${p => p.theme.colors.border};
  border-radius: 12px;
  overflow: hidden;
`;

const FormSide = styled.div`
  display: grid; place-items: center;
  padding: 56px 32px;
`;

const Card = styled.div`
  width: 100%;
  max-width: 380px;
  display: flex; flex-direction: column; gap: 18px;

  h2 {
    margin: 0;
    font-size: 22px; font-weight: 700;
    letter-spacing: -0.3px;
    color: ${p => p.theme.colors.text};
  }
  .sub { font-size: 13px; color: ${p => p.theme.colors.text2}; margin: 4px 0 0; }

  .ant-input-affix-wrapper {
    background: ${p => p.theme.colors.panel};
    border-color: ${p => p.theme.colors.border};
    border-radius: 10px;
    padding: 8px 12px;
  }
  .ant-input-affix-wrapper:hover { border-color: ${p => p.theme.colors.borderStrong}; }
  .ant-input-affix-wrapper-focused {
    border-color: ${p => p.theme.colors.accent};
    box-shadow: 0 0 0 3px ${p => p.theme.colors.accentSoft};
  }
`;

const Submit = styled(Button)`
  height: 42px;
  border-radius: 10px;
  font-weight: 600;
  font-size: 14px;
`;

const Toggle = styled.div`
  text-align: center;
  font-size: 13px;
  color: ${p => p.theme.colors.text2};
  margin-top: 4px;
  span {
    color: ${p => p.theme.colors.accent};
    cursor: pointer;
    font-weight: 600;
    margin-left: 4px;
    &:hover { text-decoration: underline; }
  }
`;

const ThemeMini = styled.button`
  position: absolute;
  top: 22px; right: 24px;
  border: 1px solid ${p => p.theme.colors.border};
  background: ${p => p.theme.colors.panel};
  border-radius: 999px;
  padding: 5px 12px;
  font-size: 11px;
  font-family: ${p => p.theme.fontFamily.mono};
  color: ${p => p.theme.colors.text2};
  cursor: pointer;
  transition: all ${p => p.theme.duration.base};
  &:hover { color: ${p => p.theme.colors.text}; border-color: ${p => p.theme.colors.borderStrong}; }
`;

const Login = () => {
  const nav = useNavigate();
  const { login } = useAuth();
  const { mode, toggle } = useThemeMode();
  const [isReg, setIsReg] = useState(false);
  const [form] = Form.useForm();
  const [submitting, setSubmitting] = useState(false);

  const md5 = (s) => { const sp = new SparkMD5(); sp.append(s); return sp.end(); };

  const onFinish = async (v) => {
    setSubmitting(true);
    try {
      if (isReg) {
        const data = await registerUser(v);
        if (data.code === 0) {
          message.success(copy.auth.regSuccess);
          setIsReg(false);
          form.setFieldsValue({ username: v.username, password: '' });
        } else if (data.code === 2) message.error(copy.auth.regUserExists);
        else if (data.code === 6) message.error(copy.auth.regNickExists);
      } else {
        const data = await loginUser(v.username, md5(v.password));
        message.success(copy.auth.loginSuccess);
        login({ username: v.username, token: data.token });
        nav('/');
      }
    } catch (e) {
      message.error(e.message === '登录失败' ? copy.auth.loginFail : copy.auth.networkFail);
    } finally { setSubmitting(false); }
  };

  return (
    <Page>
      <Hero>
        <Brand>
          <Logo size={30} withWordmark wordmarkSize={20} />
        </Brand>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 18 }}>
          <Headline>分布式存储 +<br /><span>AI 双链知识图谱</span></Headline>
          <Sub>
            把每个上传的文件变成一个可被语义搜索、可以被双向链接的知识节点。
            为大文件提供分片与秒传，为小文件提供 AI 摘要与自动标签。
          </Sub>
          <Capsules>
            <span>FastDFS</span><span>Faiss Vector</span><span>Bi-Link Graph</span><span>Chunk Upload</span>
          </Capsules>
        </div>
        <GraphBox>
          <MiniGraph data={MOCK_GRAPH} height={260} animated={false} />
        </GraphBox>
      </Hero>

      <FormSide style={{ position: 'relative' }}>
        <ThemeMini onClick={toggle}>{mode === 'dark' ? '☀ Light' : '🌙 Dark'}</ThemeMini>
        <Card>
          <div>
            <h2>{isReg ? '创建账号' : '欢迎回来'}</h2>
            <p className="sub">{isReg ? '注册以开启你的云端知识空间' : '继续探索你的知识图谱'}</p>
          </div>
          <Form form={form} onFinish={onFinish} size="large" autoComplete="off">
            {isReg && (
              <>
                <Form.Item name="nickname" rules={[{ required: true, message: '请输入昵称' }]}>
                  <Input prefix={<UserOutlined />} placeholder="昵称" />
                </Form.Item>
                <Form.Item name="email" rules={[{ required: true, message: '请输入邮箱' }, { type: 'email', message: '邮箱格式不正确' }]}>
                  <Input prefix={<MailOutlined />} placeholder="邮箱" />
                </Form.Item>
                <Form.Item name="phone" rules={[{ required: true, message: '请输入手机号' }, { pattern: /^1[3-9]\d{9}$/, message: '手机号格式不正确' }]}>
                  <Input prefix={<PhoneOutlined />} placeholder="手机号码" />
                </Form.Item>
              </>
            )}
            <Form.Item name="username" rules={[{ required: true, message: '请输入用户名' }]}>
              <Input prefix={<UserOutlined />} placeholder="用户名" />
            </Form.Item>
            <Form.Item name="password" rules={[{ required: true, message: '请输入密码' }]}>
              <Input.Password prefix={<LockOutlined />} placeholder="密码" />
            </Form.Item>
            {isReg && (
              <Form.Item
                name="confirmPassword"
                dependencies={['password']}
                rules={[
                  { required: true, message: '请确认密码' },
                  ({ getFieldValue }) => ({
                    validator(_, value) {
                      if (!value || getFieldValue('password') === value) return Promise.resolve();
                      return Promise.reject(new Error('两次输入的密码不一致'));
                    },
                  }),
                ]}
              >
                <Input.Password prefix={<LockOutlined />} placeholder="确认密码" />
              </Form.Item>
            )}
            <Form.Item style={{ marginBottom: 0 }}>
              <Submit type="primary" htmlType="submit" block loading={submitting}>
                {isReg ? '注册' : '登录'}
              </Submit>
            </Form.Item>
          </Form>
          <Toggle>
            {isReg ? '已有账号？' : '没有账号？'}
            <span onClick={() => { setIsReg(!isReg); form.resetFields(); }}>
              {isReg ? '立即登录' : '立即注册'}
            </span>
          </Toggle>
        </Card>
      </FormSide>
    </Page>
  );
};

export default Login;
