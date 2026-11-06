package com.fusionautomate.aanav

import kotlinx.coroutines.flow.MutableSharedFlow

/** Simple in-process bus from the notification listener to the BLE service. */
object NavBus {
    val updates = MutableSharedFlow<NavParser.Update>(extraBufferCapacity = 32)

    fun emit(update: NavParser.Update) {
        updates.tryEmit(update)
    }
}
