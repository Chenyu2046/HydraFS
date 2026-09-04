import React, { useEffect, useState } from 'react';
import { useAuth } from '../contexts/AuthContext';

const AuthenticatedImage = ({ file, alt, ...props }) => {
  const { user } = useAuth();
  const [src, setSrc] = useState('');

  useEffect(() => {
    let active = true;
    let objectUrl = null;
    const source = file?.url || '';
    const needsAuth = file?.storage_mode === 'manifest' && file?.object_id;

    setSrc(needsAuth ? '' : source);
    if (!needsAuth || !user?.username || !user?.token) {
      return () => {};
    }

    fetch(source, {
      headers: {
        'X-Upload-User': user.username,
        'X-Upload-Token': user.token,
      },
    })
      .then(response => {
        if (!response.ok) throw new Error('image preview failed');
        return response.blob();
      })
      .then(blob => {
        objectUrl = URL.createObjectURL(blob);
        if (active) setSrc(objectUrl);
      })
      .catch(() => {});

    return () => {
      active = false;
      if (objectUrl) URL.revokeObjectURL(objectUrl);
    };
  }, [file?.url, file?.storage_mode, file?.object_id, user?.username, user?.token]);

  return src ? <img src={src} alt={alt} {...props} /> : null;
};

export default AuthenticatedImage;
