import { ReactNode } from 'react';
import { Sidebar } from './Sidebar';

export function Layout({ children }: { children: ReactNode }) {
  //--- Render mode: when the URL carries ?_render=table the page is being
  //--- captured by the scheduler's headless browser for a Telegram
  //--- screenshot. Strip the sidebar + outer padding so the screenshot
  //--- focuses on the report content instead of the app chrome.
  const renderMode = new URLSearchParams(window.location.search).get('_render');
  if (renderMode === 'table') {
    //--- Headless-render mode: fill the viewport and pin a brand footer
    //--- to the bottom so every Telegram-bound screenshot ends with the
    //--- "powered by bitaker.io" credit regardless of table height.
    return (
      <main className="bg-white min-h-screen pb-10 relative">
        <div className="px-4 py-3">{children}</div>
        <div className="fixed bottom-0 left-0 right-0 text-center text-[10px] text-ink-400 py-2 bg-white/90 border-t border-ink-100">
          powered by bitaker.io
        </div>
      </main>
    );
  }
  return (
    <div className="flex h-screen overflow-hidden">
      <Sidebar />
      <main className="flex-1 overflow-y-auto">
        <div className="max-w-[1400px] mx-auto px-8 py-8">{children}</div>
      </main>
    </div>
  );
}
