import React from 'react';
import styled from '@emotion/styled';
import { ArrowRightOutlined } from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';

const Card = styled.button`
  text-align: left;
  background: ${p => p.theme.colors.panel};
  border: 1px solid ${p => p.theme.colors.border};
  border-radius: 12px;
  padding: 22px 22px 18px;
  cursor: pointer;
  transition: all ${p => p.theme.duration.base} ${p => p.theme.ease.out};
  display: flex; flex-direction: column; gap: 10px;
  position: relative;
  overflow: hidden;
  font: inherit;
  color: inherit;

  &::before {
    content: '';
    position: absolute; inset: 0;
    background: radial-gradient(circle at 0% 0%, ${p => p.theme.colors.accentSoft} 0%, transparent 50%);
    opacity: 0;
    transition: opacity ${p => p.theme.duration.base} ${p => p.theme.ease.out};
    pointer-events: none;
  }
  &:hover {
    border-color: ${p => p.theme.colors.borderStrong};
    transform: translateY(-2px);
    box-shadow: ${p => p.theme.shadow.md};
  }
  &:hover::before { opacity: 1; }
  &:hover .arrow { transform: translateX(4px); color: ${p => p.theme.colors.accent}; }

  .head {
    display: flex; align-items: center; gap: 10px;
  }
  .icon-wrap {
    width: 36px; height: 36px;
    display: grid; place-items: center;
    border-radius: 9px;
    background: ${p => p.theme.colors.accentSoft};
    color: ${p => p.theme.colors.accent};
    font-size: 18px;
  }
  .tag {
    margin-left: auto;
    font-size: 10.5px;
    font-family: ${p => p.theme.fontFamily.mono};
    color: ${p => p.theme.colors.text3};
    letter-spacing: 0.4px;
    text-transform: uppercase;
  }
  h3 {
    margin: 0;
    font-size: 15.5px;
    font-weight: 600;
    color: ${p => p.theme.colors.text};
    letter-spacing: -0.2px;
  }
  p {
    margin: 0;
    font-size: 12.5px;
    line-height: 1.55;
    color: ${p => p.theme.colors.text2};
  }
  .foot {
    margin-top: 6px;
    display: flex; align-items: center; justify-content: space-between;
    font-size: 12px;
    color: ${p => p.theme.colors.text2};
  }
  .arrow {
    transition: transform ${p => p.theme.duration.base} ${p => p.theme.ease.out},
                color ${p => p.theme.duration.base} ${p => p.theme.ease.out};
  }
`;

const CapabilityCard = ({ icon, title, desc, tag, to, footer }) => {
  const nav = useNavigate();
  return (
    <Card onClick={() => to && nav(to)}>
      <div className="head">
        <div className="icon-wrap">{icon}</div>
        <span className="tag">{tag}</span>
      </div>
      <h3>{title}</h3>
      <p>{desc}</p>
      <div className="foot">
        <span>{footer}</span>
        <ArrowRightOutlined className="arrow" />
      </div>
    </Card>
  );
};

export default CapabilityCard;
