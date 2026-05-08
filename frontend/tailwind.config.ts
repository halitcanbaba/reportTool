import type { Config } from 'tailwindcss';

export default {
  content: ['./index.html', './src/**/*.{ts,tsx}'],
  theme: {
    extend: {
      colors: {
        ink: { 50:'#f6f7f9',100:'#eceef2',200:'#d5dae3',300:'#aab2c1',400:'#7d869b',500:'#5b6479',600:'#4a5061',700:'#3d4250',800:'#262932',900:'#15171c' }
      },
      fontFamily: {
        sans: ['Inter', 'ui-sans-serif', 'system-ui'],
        mono: ['JetBrains Mono', 'ui-monospace', 'monospace'],
      },
    },
  },
  plugins: [],
} satisfies Config;
