import { Routes, Route, Navigate } from 'react-router-dom';
import { Layout } from './components/Layout';
import { ManagerListPage } from './pages/ManagerListPage';
import { ManagerEditPage } from './pages/ManagerEditPage';
import { AccountFilterListPage } from './pages/AccountFilterListPage';
import { AccountFilterEditPage } from './pages/AccountFilterEditPage';
import { TemplateListPage } from './pages/TemplateListPage';
import { TemplateDesignerPage } from './pages/TemplateDesignerPage';
import { BlueprintListPage } from './pages/BlueprintListPage';
import { BlueprintEditPage } from './pages/BlueprintEditPage';
import { ReadyMadeListPage } from './pages/ReadyMadeListPage';
import { ReadyMadeEditPage } from './pages/ReadyMadeEditPage';
import { ScheduleListPage } from './pages/ScheduleListPage';
import { ScheduleEditPage } from './pages/ScheduleEditPage';
import { RunReportPage } from './pages/RunReportPage';
import { ResultViewPage } from './pages/ResultViewPage';
import { DownloadHistoryPage } from './pages/DownloadHistoryPage';

export default function App() {
  return (
    <Layout>
      <Routes>
        <Route path="/" element={<Navigate to="/templates" replace />} />
        <Route path="/managers" element={<ManagerListPage />} />
        <Route path="/managers/new" element={<ManagerEditPage />} />
        <Route path="/managers/:id/edit" element={<ManagerEditPage />} />
        <Route path="/account-filters" element={<AccountFilterListPage />} />
        <Route path="/account-filters/new" element={<AccountFilterEditPage />} />
        <Route path="/account-filters/:id/edit" element={<AccountFilterEditPage />} />
        <Route path="/blueprints" element={<BlueprintListPage />} />
        <Route path="/blueprints/new" element={<BlueprintEditPage />} />
        <Route path="/blueprints/:id/edit" element={<BlueprintEditPage />} />
        <Route path="/templates" element={<TemplateListPage />} />
        <Route path="/templates/new" element={<TemplateDesignerPage />} />
        <Route path="/templates/:id/edit" element={<TemplateDesignerPage />} />
        <Route path="/templates/:id/run" element={<RunReportPage />} />
        <Route path="/ready-made" element={<ReadyMadeListPage />} />
        <Route path="/ready-made/new" element={<ReadyMadeEditPage />} />
        <Route path="/ready-made/:id/edit" element={<ReadyMadeEditPage />} />
        <Route path="/schedules" element={<ScheduleListPage />} />
        <Route path="/schedules/new" element={<ScheduleEditPage />} />
        <Route path="/schedules/:id/edit" element={<ScheduleEditPage />} />
        <Route path="/jobs/:id" element={<ResultViewPage />} />
        <Route path="/reports" element={<Navigate to="/templates" replace />} />
        <Route path="/history" element={<DownloadHistoryPage />} />
      </Routes>
    </Layout>
  );
}
