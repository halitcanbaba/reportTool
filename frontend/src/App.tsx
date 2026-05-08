import { Routes, Route, Navigate } from 'react-router-dom';
import { Layout } from './components/Layout';
import { ManagerListPage } from './pages/ManagerListPage';
import { ManagerEditPage } from './pages/ManagerEditPage';
import { ReportRunnerPage } from './pages/ReportRunnerPage';
import { DownloadHistoryPage } from './pages/DownloadHistoryPage';

export default function App() {
  return (
    <Layout>
      <Routes>
        <Route path="/" element={<Navigate to="/managers" replace />} />
        <Route path="/managers" element={<ManagerListPage />} />
        <Route path="/managers/new" element={<ManagerEditPage />} />
        <Route path="/managers/:id/edit" element={<ManagerEditPage />} />
        <Route path="/reports" element={<ReportRunnerPage />} />
        <Route path="/history" element={<DownloadHistoryPage />} />
      </Routes>
    </Layout>
  );
}
