package dev.geronimodesenvolvimentos.krakatoa

import android.app.Activity
import android.content.Context
import android.content.res.AssetManager
import android.view.Choreographer
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView

class VulkanSurfaceView(context: Context,var assetManager: AssetManager,var activity: Activity):
    SurfaceView(context), SurfaceHolder.Callback{

    /**
     * A native method that is implemented by the 'krakatoa' native library,
     * which is packaged with this application.
     */

    companion object {
        init {
            //TODO: Switch between debug version and release version based on build type. Debug version ends with 'd' while release doesn't.
            System.loadLibrary("krakatoad")
        }
    }

    private var renderThread: RenderThread? = null
    private var surfaceReady = false
    private var arcoreActivated = false
    init {
        holder.addCallback(this)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        nativeOnSurfaceCreated(holder.surface, assetManager, activity)
        renderThread = RenderThread()
        surfaceReady = true
        (context as? MainActivity)?.let {
            activateArcore(it, it);
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        val rotation = (context as Activity).windowManager.defaultDisplay.rotation
        nativeOnSurfaceChanged(width, height, rotation)
        renderThread?.start()
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        renderThread?.stopRendering()
        renderThread = null
        surfaceReady = false
        arcoreActivated = false
        nativeOnSurfaceDestroyed()
    }
    fun activateArcore(ctx:MainActivity,activity: MainActivity) {
        if (surfaceReady && activity.isCameraPermissionGranted() && !arcoreActivated) {
            nativeActivateArcore(activity, activity)
            arcoreActivated = true
        }
    }
    fun cleanup() {
        renderThread?.stopRendering()
        renderThread = null
        nativeOnSurfaceDestroyed()
        nativeCleanup()
    }
    /**
     * It's the render thread that calls the native draw methods. Remember that the app is multithreaded
     * and that one can't simply update things in the android main thread.
     * */
    private inner class RenderThread : Thread() {
        @Volatile
        private var running = true

        override fun run() {
            while (running) {
                nativeOnDrawFrame()
            }
        }

        fun stopRendering() {
            running = false
            join()
        }
    }
    fun onResume() {
        nativeOnResume()
    }

    fun onPause() {
        nativeOnPause()
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                nativeOnTouchEvent(event.x, event.y, 0) // 0 = DOWN
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                nativeOnTouchEvent(event.x, event.y, 1) // 1 = MOVE
                return true
            }
            MotionEvent.ACTION_UP -> {
                nativeOnTouchEvent(event.x, event.y, 2) // 2 = UP
                return true
            }
        }
        return super.onTouchEvent(event)
    }

    private external fun nativeOnSurfaceCreated(surface: Surface, assetManager: AssetManager, activity: Activity)
    private external fun nativeOnSurfaceChanged(width: Int, height: Int, rotation: Int)
    private external fun nativeOnSurfaceDestroyed()
    private external fun nativeOnDrawFrame()
    private external fun nativeCleanup()
    private external fun nativeOnResume()
    private external fun nativeOnPause()
    private external fun nativeActivateArcore(context: Context, activity: Activity)
    private external fun nativeOnTouchEvent(x: Float, y: Float, action: Int)
}