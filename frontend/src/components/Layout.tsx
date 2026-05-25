import { ReactNode } from 'react';
import { Sidebar } from './Sidebar';

export function Layout({ children }: { children: ReactNode }) {
  //--- Render mode: when the URL carries ?_render=table the page is being
  //--- captured by the scheduler's headless browser for a Telegram
  //--- screenshot. Strip the sidebar + outer padding so the screenshot
  //--- focuses on the report content instead of the app chrome.
  const renderMode = new URLSearchParams(window.location.search).get('_render');
  if (renderMode === 'table') {
    return (
      <main className="bg-white">
        <div className="px-4 py-3">{children}</div>
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
