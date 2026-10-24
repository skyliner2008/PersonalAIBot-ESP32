package com.skyliner2008.jarvis.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import com.skyliner2008.jarvis.update.AppSelfUpdate

/** Android: the app installs its own update from GitHub through PackageInstaller (AppSelfUpdate, T097). */
@Composable
actual fun rememberAppUpdater(): AppUpdater {
    val context = LocalContext.current.applicationContext
    return remember(context) {
        AppUpdater(
            supported = true,
            currentVersion = AppSelfUpdate.currentVersion(context),
            state = AppSelfUpdate.state,
            check = { AppSelfUpdate.check(context) },
            update = { AppSelfUpdate.update(context) },
            cancel = { AppSelfUpdate.cancel() },
        )
    }
}
