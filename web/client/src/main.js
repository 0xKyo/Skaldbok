import { createApp } from 'vue';
import App from './App.vue';
import { apiHealth } from './api.js';
import { lang, useDefault } from './i18n.js';
import './style.css';

// The page speaks the GM's language until somebody picks another in this browser: ask the server which, before anything is asked
// for (an unreachable server leaves it as it is).
apiHealth().then((health) => {
  if (health?.language) useDefault(health.language);
  document.documentElement.lang = lang.value;
  createApp(App).mount('#app');
});
