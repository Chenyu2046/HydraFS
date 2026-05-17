import styled from '@emotion/styled';

/**
 * 通用面板：单边框、可 hover 抬升
 */
export const Panel = styled.div`
  background: ${p => p.theme.colors.panel};
  border: 1px solid ${p => p.theme.colors.border};
  border-radius: 12px;
  transition: border-color ${p => p.theme.duration.base} ${p => p.theme.ease.out},
              transform ${p => p.theme.duration.base} ${p => p.theme.ease.out},
              box-shadow ${p => p.theme.duration.base} ${p => p.theme.ease.out};
  ${p => p.$hoverable && `
    &:hover {
      border-color: ${p.theme.colors.borderStrong};
      transform: translateY(-2px);
      box-shadow: ${p.theme.shadow.md};
    }
  `}
`;

export const PanelHeader = styled.div`
  display: flex; align-items: center; gap: 12px;
  padding: 14px 18px;
  border-bottom: 1px solid ${p => p.theme.colors.border};

  h3 {
    margin: 0;
    font-size: 14px;
    font-weight: 600;
    color: ${p => p.theme.colors.text};
    letter-spacing: -0.1px;
  }
  .subtitle {
    font-size: 12px;
    color: ${p => p.theme.colors.text2};
  }
  .right { margin-left: auto; }
`;

export const PanelBody = styled.div`
  padding: ${p => p.$pad ?? '18px'};
`;

export const SectionTitle = styled.div`
  display: flex; align-items: baseline; gap: 12px;
  margin: 28px 0 14px;
  h2 {
    margin: 0;
    font-size: 17px;
    font-weight: 600;
    color: ${p => p.theme.colors.text};
    letter-spacing: -0.2px;
  }
  span {
    color: ${p => p.theme.colors.text3};
    font-size: 12px;
  }
`;

export const Pill = styled.span`
  display: inline-flex; align-items: center; gap: 6px;
  padding: 3px 8px;
  border-radius: 999px;
  background: ${p => p.theme.colors.accentSoft};
  color: ${p => p.theme.colors.accent};
  font-size: 11px;
  font-weight: 500;
  letter-spacing: 0.2px;
  border: 1px solid ${p => p.theme.colors.accentBorder};
`;

export const Dot = styled.span`
  width: 6px; height: 6px;
  border-radius: 999px;
  background: ${p => p.color || p.theme.colors.accent};
  display: inline-block;
`;

export const HSep = styled.div`
  height: 1px;
  background: ${p => p.theme.colors.border};
  margin: ${p => p.$m || '12px 0'};
`;
