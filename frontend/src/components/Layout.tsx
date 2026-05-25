import { ReactNode } from 'react';
import { Sidebar } from './Sidebar';

export function Layout({ children }: { children: ReactNode }) {
  //--- Render mode: when the URL carries ?_render=table the page is being
  //--- captured by the scheduler's headless browser for a Telegram
  //--- screenshot. Strip the sidebar + outer padding so the screenshot
  //--- focuses on the report content instead of the app chrome.
  const renderMode = new URLSearchParams(window.location.search).get('_render');
  if (renderMode === 'table') {
    //--- Headless-render mode: footer sits at the end of the document
    //--- flow inside a flex column. Headless Chrome's --screenshot in
    //--- --headless=new mode captures the full page; a fixed-position
    //--- footer would land at viewport bottom (mid-screenshot for tall
    //--- tables) and could be cropped entirely on short pages. flex-1
    //--- on the body pushes the footer to viewport bottom when the
    //--- content is shorter than the screen.
    return (
      <main className="bg-white min-h-screen flex flex-col">
        <div className="flex-1 px-4 py-3">{children}</div>
        <footer className="text-center text-[11px] text-ink-400 py-2 border-t border-ink-100 mt-2">
          powered by bitaker.io
        </footer>
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
