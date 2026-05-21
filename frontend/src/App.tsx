import { Routes, Route, Navigate, Outlet } from 'react-router-dom';
import { Layout } from './components/Layout';
import { RequireAuth } from './components/RequireAuth';
import { RequireAdmin } from './components/RequireAdmin';
import { LoginPage } from './pages/LoginPage';
import { SetupPage } from './pages/SetupPage';
import { ChangePasswordPage } from './pages/ChangePasswordPage';
import { ManagerListPage } from './pages/ManagerListPage';
import { ManagerEditPage } from './pages/ManagerEditPage';
import { AccountFilterListPage } from './pages/AccountFilterListPage';
import { AccountFilterEditPage } from './pages/AccountFilterEditPage';
import { DealFilterListPage } from './pages/DealFilterListPage';
import { DealFilterEditPage } from './pages/DealFilterEditPage';
import { TemplateListPage } from './pages/TemplateListPage';
import { TemplateDesignerPage } from './pages/TemplateDesignerPage';
import { BlueprintListPage } from './pages/BlueprintListPage';
import { BlueprintEditPage } from './pages/BlueprintEditPage';
import { ReadyMadeListPage } from './pages/ReadyMadeListPage';
import { ReadyMadeEditPage } from './pages/ReadyMadeEditPage';
import { ScheduleListPage } from './pages/ScheduleListPage';
import { ScheduleEditPage } from './pages/ScheduleEditPage';
import { UserListPage } from './pages/UserListPage';
import { UserEditPage } from './pages/UserEditPage';
import { RunReportPage } from './pages/RunReportPage';
import { ResultViewPage } from './pages/ResultViewPage';
import { DownloadHistoryPage } from './pages/DownloadHistoryPage';

//--- The Layout wrapper renders the sidebar; we render it INSIDE RequireAuth so
//--- unauthenticated routes (/login, /setup) get a clean full-screen view.
function AppShell() {
  return (
    <RequireAuth>
      <Layout>
        <Outlet />
      </Layout>
    </RequireAuth>
  );
}

function AdminOnly() {
  return <RequireAdmin><Outlet /></RequireAdmin>;
}

export default function App() {
  return (
    <Routes>
      <Route path="/setup" element={<SetupPage />} />
      <Route path="/login" element={<LoginPage />} />

      <Route element={<AppShell />}>
        <Route path="/" element={<Navigate to="/templates" replace />} />
        <Route path="/managers" element={<ManagerListPage />} />
        <Route path="/managers/new" element={<ManagerEditPage />} />
        <Route path="/managers/:id/edit" element={<ManagerEditPage />} />
        <Route path="/account-filters" element={<AccountFilterListPage />} />
        <Route path="/account-filters/new" element={<AccountFilterEditPage />} />
        <Route path="/account-filters/:id/edit" element={<AccountFilterEditPage />} />
        <Route path="/deal-filters" element={<DealFilterListPage />} />
        <Route path="/deal-filters/new" element={<DealFilterEditPage />} />
        <Route path="/deal-filters/:id/edit" element={<DealFilterEditPage />} />
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
        <Route path="/account/password" element={<ChangePasswordPage />} />

        <Route element={<AdminOnly />}>
          <Route path="/users" element={<UserListPage />} />
          <Route path="/users/new" element={<UserEditPage />} />
          <Route path="/users/:id/edit" element={<UserEditPage />} />
        </Route>
      </Route>
    </Routes>
  );
}
