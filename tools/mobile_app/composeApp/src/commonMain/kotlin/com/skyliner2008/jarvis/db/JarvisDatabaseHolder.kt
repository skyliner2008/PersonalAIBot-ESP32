package com.skyliner2008.jarvis.db

import com.skyliner2008.jarvis.automation.AutomationManager

object JarvisDatabaseHolder {
    @Volatile
    private var _database: JarvisDatabase? = null

    @Volatile
    private var _automationManager: AutomationManager? = null

    val database: JarvisDatabase?
        get() = _database

    fun install(db: JarvisDatabase) {
        _database = db
    }

    /**
     * One database for the whole process (T093): the activity (JarvisViewModel, made again every time the app is
     * reopened while its services keep the process alive) and JarvisAutomationService (started at every app start)
     * each opened their own driver and never closed it - logcat "SQLiteConnection ... was leaked!".
     * A restore restarts the process (DatabaseBackupManager.restartApp), so a pending restore is still applied first.
     */
    fun getOrCreate(create: () -> JarvisDatabase): JarvisDatabase =
        _database ?: synchronized(this) { _database ?: create().also { _database = it } }

    /** Returns a singleton AutomationManager backed by the installed database.
     *  Must call [install] first. */
    fun getAutomationManager(): AutomationManager {
        val db = _database ?: throw IllegalStateException("Database not installed")
        return _automationManager ?: synchronized(this) {
            _automationManager ?: AutomationManager(db).also { _automationManager = it }
        }
    }
}
