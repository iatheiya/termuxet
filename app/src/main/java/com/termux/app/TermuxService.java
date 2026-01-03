package com.termux.app;

import android.annotation.SuppressLint;
import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;

import androidx.annotation.Nullable;

import com.termux.app.event.SystemEventReceiver;
import com.termux.app.service.ServiceExecutionManager;
import com.termux.app.service.ServiceNotificationManager;
import com.termux.app.service.ServiceWakeLockManager;
import com.termux.app.terminal.TermuxTerminalSessionActivityClient;
import com.termux.app.terminal.TermuxTerminalSessionServiceClient;
import com.termux.shared.data.DataUtils;
import com.termux.shared.data.IntentUtils;
import com.termux.shared.logger.Logger;
import com.termux.shared.shell.command.ExecutionCommand;
import com.termux.shared.shell.command.ExecutionCommand.Runner;
import com.termux.shared.shell.command.runner.app.AppShell;
import com.termux.shared.termux.TermuxConstants;
import com.termux.shared.termux.TermuxConstants.TERMUX_APP.TERMUX_SERVICE;
import com.termux.shared.termux.plugins.TermuxPluginUtils;
import com.termux.shared.termux.settings.preferences.TermuxAppSharedPreferences;
import com.termux.shared.termux.settings.properties.TermuxAppSharedProperties;
import com.termux.shared.termux.shell.TermuxShellManager;
import com.termux.shared.termux.shell.TermuxShellUtils;
import com.termux.shared.termux.shell.command.runner.terminal.TermuxSession;
import com.termux.shared.termux.terminal.TermuxTerminalSessionClientBase;
import com.termux.terminal.TerminalSession;

import java.util.List;

public final class TermuxService extends Service implements AppShell.AppShellClient, TermuxSession.TermuxSessionClient {

    class LocalBinder extends Binder {
        public final TermuxService service = TermuxService.this;
    }

    private final IBinder mBinder = new LocalBinder();
    private final Handler mHandler = new Handler();
    private TermuxTerminalSessionActivityClient mTermuxTerminalSessionActivityClient;
    private final TermuxTerminalSessionServiceClient mTermuxTerminalSessionServiceClient = new TermuxTerminalSessionServiceClient(this);
    private TermuxAppSharedProperties mProperties;
    private TermuxShellManager mShellManager;
    private boolean mWantsToStop = false;
    private static final String LOG_TAG = "TermuxService";

    private ServiceWakeLockManager mWakeLockManager;
    private ServiceNotificationManager mNotificationManager;
    private ServiceExecutionManager mExecutionManager;

    static {
        System.loadLibrary("termux_loader");
    }

    private native int nativeStartSession(String executablePath, String[] arguments, String[] environment);

    @Override
    public void onCreate() {
        Logger.logVerbose(LOG_TAG, "onCreate");
        mProperties = TermuxAppSharedProperties.getProperties();
        mShellManager = TermuxShellManager.getShellManager();

        mWakeLockManager = new ServiceWakeLockManager(this);
        mNotificationManager = new ServiceNotificationManager(this);
        mExecutionManager = new ServiceExecutionManager(this, mShellManager);

        mNotificationManager.setWakeLockManager(mWakeLockManager);

        runStartForeground();
        SystemEventReceiver.registerPackageUpdateEvents(this);
    }

    @SuppressLint("Wakelock")
    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Logger.logDebug(LOG_TAG, "onStartCommand");
        runStartForeground();
        String action = null;
        if (intent != null) {
            Logger.logVerboseExtended(LOG_TAG, "Intent Received:\n" + IntentUtils.getIntentString(intent));
            action = intent.getAction();
        }
        if (action != null) {
            switch (action) {
                case TERMUX_SERVICE.ACTION_STOP_SERVICE:
                    Logger.logDebug(LOG_TAG, "ACTION_STOP_SERVICE intent received");
                    actionStopService();
                    break;
                case TERMUX_SERVICE.ACTION_WAKE_LOCK:
                    Logger.logDebug(LOG_TAG, "ACTION_WAKE_LOCK intent received");
                    mWakeLockManager.acquireWakeLock();
                    break;
                case TERMUX_SERVICE.ACTION_WAKE_UNLOCK:
                    Logger.logDebug(LOG_TAG, "ACTION_WAKE_UNLOCK intent received");
                    mWakeLockManager.releaseWakeLock(true);
                    break;
                case TERMUX_SERVICE.ACTION_SERVICE_EXECUTE:
                    Logger.logDebug(LOG_TAG, "ACTION_SERVICE_EXECUTE intent received");
                    mExecutionManager.actionServiceExecute(intent);
                    break;
                default:
                    Logger.logError(LOG_TAG, "Invalid action: \"" + action + "\"");
                    break;
            }
        }
        return Service.START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        Logger.logVerbose(LOG_TAG, "onDestroy");
        TermuxShellUtils.clearTermuxTMPDIR(true);
        mWakeLockManager.releaseWakeLock(false);
        if (!mWantsToStop) mExecutionManager.killAllTermuxExecutionCommands();
        TermuxShellManager.onAppExit(this);
        SystemEventReceiver.unregisterPackageUpdateEvents(this);
        runStopForeground();
    }

    @Override
    public IBinder onBind(Intent intent) {
        Logger.logVerbose(LOG_TAG, "onBind");
        return mBinder;
    }

    @Override
    public boolean onUnbind(Intent intent) {
        Logger.logVerbose(LOG_TAG, "onUnbind");
        if (mTermuxTerminalSessionActivityClient != null) unsetTermuxTerminalSessionClient();
        return false;
    }

    private void runStartForeground() {
        mNotificationManager.setupNotificationChannel();
        startForeground(TermuxConstants.TERMUX_APP_NOTIFICATION_ID, mNotificationManager.buildNotification());
    }

    private void runStopForeground() {
        stopForeground(true);
    }

    public void requestStopService() {
        Logger.logDebug(LOG_TAG, "Requesting to stop service");
        runStopForeground();
        stopSelf();
    }

    private void actionStopService() {
        mWantsToStop = true;
        mExecutionManager.killAllTermuxExecutionCommands();
        requestStopService();
    }

    public void updateNotification() {
        mNotificationManager.updateNotification();
    }

    @Nullable
    public AppShell createTermuxTask(String executablePath, String[] arguments, String stdin, String workingDirectory) {
        return mExecutionManager.createTermuxTask(new ExecutionCommand(TermuxShellManager.getNextShellId(), executablePath, arguments, stdin, workingDirectory, Runner.APP_SHELL.getName(), false));
    }

    @Override
    public void onAppShellExited(final AppShell termuxTask) {
        mHandler.post(() -> {
            if (termuxTask != null) {
                ExecutionCommand executionCommand = termuxTask.getExecutionCommand();
                Logger.logVerbose(LOG_TAG, "The onTermuxTaskExited() callback called for \"" + executionCommand.getCommandIdAndLabelLogString() + "\" TermuxTask command");
                if (executionCommand != null && executionCommand.isPluginExecutionCommand) TermuxPluginUtils.processPluginExecutionCommandResult(this, LOG_TAG, executionCommand);
                mShellManager.mTermuxTasks.remove(termuxTask);
            }
            updateNotification();
        });
    }

    @Nullable
    public TermuxSession createTermuxSession(String executablePath, String[] arguments, String stdin, String workingDirectory, boolean isFailSafe, String sessionName) {
        ExecutionCommand executionCommand = new ExecutionCommand(TermuxShellManager.getNextShellId(), executablePath, arguments, stdin, workingDirectory, Runner.TERMINAL_SESSION.getName(), isFailSafe);
        executionCommand.shellName = sessionName;
        return mExecutionManager.createTermuxSession(executionCommand);
    }

    public synchronized int removeTermuxSession(TerminalSession sessionToRemove) {
        int index = getIndexOfSession(sessionToRemove);
        if (index >= 0) mShellManager.mTermuxSessions.get(index).finish();
        return index;
    }

    @Override
    public void onTermuxSessionExited(final TermuxSession termuxSession) {
        if (termuxSession != null) {
            ExecutionCommand executionCommand = termuxSession.getExecutionCommand();
            Logger.logVerbose(LOG_TAG, "The onTermuxSessionExited() callback called for \"" + executionCommand.getCommandIdAndLabelLogString() + "\" TermuxSession command");
            if (executionCommand != null && executionCommand.isPluginExecutionCommand) TermuxPluginUtils.processPluginExecutionCommandResult(this, LOG_TAG, executionCommand);
            mShellManager.mTermuxSessions.remove(termuxSession);
            if (mTermuxTerminalSessionActivityClient != null) mTermuxTerminalSessionActivityClient.termuxSessionListNotifyUpdated();
        }
        updateNotification();
    }

    public synchronized TermuxTerminalSessionClientBase getTermuxTerminalSessionClient() {
        if (mTermuxTerminalSessionActivityClient != null) return mTermuxTerminalSessionActivityClient;
        else return mTermuxTerminalSessionServiceClient;
    }

    public synchronized void setTermuxTerminalSessionClient(TermuxTerminalSessionActivityClient termuxTerminalSessionActivityClient) {
        mTermuxTerminalSessionActivityClient = termuxTerminalSessionActivityClient;
        for (int i = 0; i < mShellManager.mTermuxSessions.size(); i++) mShellManager.mTermuxSessions.get(i).getTerminalSession().updateTerminalSessionClient(mTermuxTerminalSessionActivityClient);
    }

    public synchronized void unsetTermuxTerminalSessionClient() {
        for (int i = 0; i < mShellManager.mTermuxSessions.size(); i++) mShellManager.mTermuxSessions.get(i).getTerminalSession().updateTerminalSessionClient(mTermuxTerminalSessionServiceClient);
        mTermuxTerminalSessionActivityClient = null;
    }

    public void setCurrentStoredTerminalSession(TerminalSession terminalSession) {
        if (terminalSession == null) return;
        TermuxAppSharedPreferences preferences = TermuxAppSharedPreferences.build(this);
        if (preferences == null) return;
        preferences.setCurrentSession(terminalSession.mHandle);
    }

    public synchronized boolean isTermuxSessionsEmpty() {
        return mShellManager.mTermuxSessions.isEmpty();
    }

    public synchronized boolean isTermuxTasksEmpty() {
        return mShellManager.mTermuxTasks.isEmpty();
    }

    public synchronized int getTermuxSessionsSize() {
        return mShellManager.mTermuxSessions.size();
    }

    public synchronized int getTermuxTasksSize() {
        return mShellManager.mTermuxTasks.size();
    }

    public synchronized List<TermuxSession> getTermuxSessions() {
        return mShellManager.mTermuxSessions;
    }

    @Nullable
    public synchronized TermuxSession getTermuxSession(int index) {
        if (index >= 0 && index < mShellManager.mTermuxSessions.size()) return mShellManager.mTermuxSessions.get(index);
        else return null;
    }

    @Nullable
    public synchronized TermuxSession getTermuxSessionForTerminalSession(TerminalSession terminalSession) {
        if (terminalSession == null) return null;
        for (int i = 0; i < mShellManager.mTermuxSessions.size(); i++) {
            if (mShellManager.mTermuxSessions.get(i).getTerminalSession().equals(terminalSession)) return mShellManager.mTermuxSessions.get(i);
        }
        return null;
    }

    public synchronized TermuxSession getLastTermuxSession() {
        return mShellManager.mTermuxSessions.isEmpty() ? null : mShellManager.mTermuxSessions.get(mShellManager.mTermuxSessions.size() - 1);
    }

    public synchronized int getIndexOfSession(TerminalSession terminalSession) {
        if (terminalSession == null) return -1;
        for (int i = 0; i < mShellManager.mTermuxSessions.size(); i++) {
            if (mShellManager.mTermuxSessions.get(i).getTerminalSession().equals(terminalSession)) return i;
        }
        return -1;
    }

    public synchronized TerminalSession getTerminalSessionForHandle(String sessionHandle) {
        TerminalSession terminalSession;
        for (int i = 0, len = mShellManager.mTermuxSessions.size(); i < len; i++) {
            terminalSession = mShellManager.mTermuxSessions.get(i).getTerminalSession();
            if (terminalSession.mHandle.equals(sessionHandle)) return terminalSession;
        }
        return null;
    }

    public synchronized AppShell getTermuxTaskForShellName(String name) {
        if (DataUtils.isNullOrEmpty(name)) return null;
        AppShell appShell;
        for (int i = 0, len = mShellManager.mTermuxTasks.size(); i < len; i++) {
            appShell = mShellManager.mTermuxTasks.get(i);
            String shellName = appShell.getExecutionCommand().shellName;
            if (shellName != null && shellName.equals(name)) return appShell;
        }
        return null;
    }

    public synchronized TermuxSession getTermuxSessionForShellName(String name) {
        if (DataUtils.isNullOrEmpty(name)) return null;
        TermuxSession termuxSession;
        for (int i = 0, len = mShellManager.mTermuxSessions.size(); i < len; i++) {
            termuxSession = mShellManager.mTermuxSessions.get(i);
            String shellName = termuxSession.getExecutionCommand().shellName;
            if (shellName != null && shellName.equals(name)) return termuxSession;
        }
        return null;
    }

    public boolean wantsToStop() {
        return mWantsToStop;
    }

    public TermuxAppSharedProperties getProperties() {
        return mProperties;
    }

    public TermuxTerminalSessionActivityClient getTermuxTerminalSessionActivityClient() {
        return mTermuxTerminalSessionActivityClient;
    }
}
