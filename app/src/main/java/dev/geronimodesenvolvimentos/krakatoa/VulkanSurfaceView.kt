package dev.geronimodesenvolvimentos.krakatoa

import android.content.Context
import android.content.res.AssetManager
import android.view.Choreographer
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView

class VulkanSurfaceView(context: Context,var assetManager: AssetManager): SurfaceView(context) {

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

    private external fun initVulkan(surface: Surface): Boolean
    private external fun resizeVulkan(width:Int, height:Int)
    private external fun destroyVulkan()
    private external fun renderFrame()
    private val choreographer = Choreographer.getInstance()
    private var isRendering = false;
    /**
     * Init: add the callbacks to the surface holder.
     * When the surface is created it creates the vulkan context in the c++.
     * When the surface is changed it resizes the vulkan swap chain and other things.
     * When the surface is destroyed it destroys the vulkan context
     * */
    init {
        holder.addCallback(object: SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                // Initialize Vulkan - surface is available
                if(!initVulkan(holder.surface)){
                    throw RuntimeException("Could not get the native surface")
                }
            }

            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                // Handle resize/orientation changes
                resizeVulkan(width, height)
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                // Cleanup Vulkan resources
                destroyVulkan()
            }
        })
    }
    /**
     * Views have no onResume. The activity that holds the view must call it.
     * */
    fun onResume() {
        isRendering = true;
        choreographer.postFrameCallback(frameCallback)
    }
    /**
     * Views have no pause. The activity that holds the view must call it.
     * */
    fun onPause() {
        isRendering = false;
        choreographer.removeFrameCallback(frameCallback)
    }
    private val frameCallback = object: Choreographer.FrameCallback {
        override fun doFrame(frameTimeInNanos: Long) {
            if(isRendering){
                renderFrame()
                choreographer.postFrameCallback(this)
            }
        }
    }

    fun activateArcore(ctx:MainActivity,activity: MainActivity) {

    }

    fun cleanup() {

    }
}