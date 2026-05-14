import { Form, Input, Button, Card, message } from 'antd';
import { UserOutlined, LockOutlined, MailOutlined, PhoneOutlined } from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '../contexts/AuthContext';
import { loginUser, registerUser } from '../services/auth';
import styled from '@emotion/styled';
import React, { useState } from 'react';
import SparkMD5 from 'spark-md5';

const Wrapper = styled.div`
  display: flex;
  justify-content: center;
  align-items: center;
  min-height: 100vh;
  background: #F8FAFC;
`;

const LoginCard = styled(Card)`
  width: 100%;
  max-width: 420px;
  border-radius: 16px;
  border: 1px solid #E2E8F0;
  box-shadow: 0 1px 3px rgba(0,0,0,0.04), 0 4px 16px rgba(0,0,0,0.04);

  .ant-card-head {
    border-bottom: none;
    padding: 32px 32px 0;
    min-height: auto;
    text-align: center;
  }

  .ant-card-head-title {
    font-size: 22px;
    font-weight: 700;
    color: #0F172A;
  }

  .ant-card-body {
    padding: 28px 32px 32px;
  }

  .ant-form-item {
    margin-bottom: 16px;
  }

  .ant-input-affix-wrapper {
    border-radius: 10px;
    border-color: #E2E8F0;
    padding: 8px 12px;

    &:hover, &:focus, &-focused {
      border-color: #2563EB;
      box-shadow: 0 0 0 3px rgba(37,99,235,0.1);
    }
  }

  .register-link {
    text-align: center;
    margin-top: 20px;
    font-size: 13.5px;
    color: #64748B;
    cursor: pointer;

    &:hover {
      color: #2563EB;
    }

    b {
      color: #2563EB;
      font-weight: 600;
    }
  }
`;

const SubmitButton = styled(Button)`
  height: 42px;
  border-radius: 10px;
  font-weight: 600;
  font-size: 15px;
  background: #2563EB;
  border-color: #2563EB;
  margin-top: 4px;

  &:hover {
    background: #1D4ED8 !important;
    border-color: #1D4ED8 !important;
  }
`;

const Login = () => {
  const navigate = useNavigate();
  const { login } = useAuth();
  const [isRegister, setIsRegister] = useState(false);
  const [form] = Form.useForm();

  const calculateMD5 = (str) => {
    const spark = new SparkMD5();
    spark.append(str);
    return spark.end();
  };

  const onFinish = async (values) => {
    if (isRegister) {
      try {
        const data = await registerUser(values);
        if (data.code === 0) {
          message.success('注册成功！请使用新账号登录');
          setIsRegister(false);
          form.setFieldsValue({ username: values.username, password: '' });
        } else if (data.code === 2) {
          message.error('用户名已存在，请重新输入！');
        } else if (data.code === 6) {
          message.error('昵称已存在，请重新输入！');
        }
      } catch (error) {
        message.error('注册失败，请检查网络连接！');
        console.error('注册错误：', error);
      }
    } else {
      try {
        const encryptedPassword = calculateMD5(values.password);
        const data = await loginUser(values.username, encryptedPassword);
        message.success('登录成功！');
        login({ username: values.username, token: data.token });
        navigate('/');
      } catch (error) {
        if (error.message === '登录失败') {
          message.error('用户名或密码错误！');
        } else {
          message.error('登录失败，请检查网络连接！');
        }
        console.error('登录错误：', error);
      }
    }
  };

  return (
    <Wrapper>
      <LoginCard title={isRegister ? "创建账号" : "CloudVault"}>
        <Form
          form={form}
          name="login"
          onFinish={onFinish}
          autoComplete="off"
          size="large"
        >
          {isRegister && (
            <>
              <Form.Item name="nickname" rules={[{ required: true, message: '请输入昵称' }]}>
                <Input prefix={<UserOutlined />} placeholder="昵称" />
              </Form.Item>
              <Form.Item
                name="email"
                rules={[
                  { required: true, message: '请输入邮箱' },
                  { type: 'email', message: '请输入有效的邮箱地址' }
                ]}
              >
                <Input prefix={<MailOutlined />} placeholder="邮箱" />
              </Form.Item>
              <Form.Item
                name="phone"
                rules={[
                  { required: true, message: '请输入手机号码' },
                  { pattern: /^1[3-9]\d{9}$/, message: '请输入有效的手机号码' }
                ]}
              >
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

          {isRegister && (
            <Form.Item
              name="confirmPassword"
              dependencies={['password']}
              rules={[
                { required: true, message: '请确认密码' },
                ({ getFieldValue }) => ({
                  validator(_, value) {
                    if (!value || getFieldValue('password') === value) {
                      return Promise.resolve();
                    }
                    return Promise.reject(new Error('两次输入的密码不一致'));
                  },
                }),
              ]}
            >
              <Input.Password prefix={<LockOutlined />} placeholder="确认密码" />
            </Form.Item>
          )}

          <Form.Item>
            <SubmitButton type="primary" htmlType="submit" block>
              {isRegister ? '注册' : '登录'}
            </SubmitButton>
          </Form.Item>
        </Form>
        <div
          className="register-link"
          onClick={() => {
            setIsRegister(!isRegister);
            form.resetFields();
          }}
        >
          {isRegister ? '已有账号？<b>立即登录</b>' : '没有账号？<b>立即注册</b>'}
        </div>
      </LoginCard>
    </Wrapper>
  );
};

export default Login;
