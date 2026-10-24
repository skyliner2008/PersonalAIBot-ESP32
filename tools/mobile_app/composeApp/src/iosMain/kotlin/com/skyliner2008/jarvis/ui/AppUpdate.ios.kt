package com.skyliner2008.jarvis.ui

import androidx.compose.runtime.Composable
import kotlinx.coroutines.flow.MutableStateFlow

/** iOS: an app cannot install itself - updates come from the store */
@Composable
actual fun rememberAppUpdater(): AppUpdater =
    AppUpdater(
        supported = false, currentVersion = "", state = MutableStateFlow(AppUpdateState.Idle),
        check = {}, update = {}, cancel = {},
    )
