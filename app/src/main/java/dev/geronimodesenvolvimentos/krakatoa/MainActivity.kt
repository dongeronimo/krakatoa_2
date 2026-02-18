package dev.geronimodesenvolvimentos.krakatoa

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.util.Log
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.google.ar.core.ArCoreApk

class MainActivity : AppCompatActivity() {
    companion object {
        private const val TAG = "MainActivity"
        private const val CAMERA_PERMISSION_REQUEST_CODE = 1001
    }

    private lateinit var vulkanSurfaceView: VulkanSurfaceView
    private var cameraPermissionGranted = false
    private var arcoreInstallRequested = false

    fun isCameraPermissionGranted() = cameraPermissionGranted

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        vulkanSurfaceView = VulkanSurfaceView(this, assets, this)
        setContentView(vulkanSurfaceView)
        // Request camera permission if not already granted
        if (checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            cameraPermissionGranted = true
        } else {
            requestPermissions(arrayOf(Manifest.permission.CAMERA), CAMERA_PERMISSION_REQUEST_CODE)
        }
    }

    override fun onResume() {
        super.onResume()
        vulkanSurfaceView.onResume()
        if (cameraPermissionGranted) {
            ensureArcoreInstalled()
        }
    }

    override fun onPause() {
        vulkanSurfaceView.onPause()
        super.onPause()
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<String>,
        results: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, results)
        if (requestCode == CAMERA_PERMISSION_REQUEST_CODE) {
            if (results.isNotEmpty() && results[0] == PackageManager.PERMISSION_GRANTED) {
                cameraPermissionGranted = true
                ensureArcoreInstalled()
            } else {
                Log.e(TAG, "Camera permission denied")
                Toast.makeText(
                    this,
                    "Camera permission is required for AR. Please grant it in Settings.",
                    Toast.LENGTH_LONG
                ).show()
                finish()
            }
        }
    }

    /**
     * Checks ARCore availability and requests installation if needed.
     * On success, tells the VulkanSurfaceView to activate ARCore.
     */
    private fun ensureArcoreInstalled() {
        val availability = ArCoreApk.getInstance().checkAvailability(this)
        if (availability.isTransient) {
            // Re-query at next onResume; the check is still in progress.
            return
        }
        if (!availability.isSupported) {
            Log.e(TAG, "ARCore is not supported on this device: $availability")
            Toast.makeText(
                this,
                "This device does not support AR.",
                Toast.LENGTH_LONG
            ).show()
            finish()
            return
        }
        // ARCore is supported. Request install / update if necessary.
        try {
            val installStatus = ArCoreApk.getInstance().requestInstall(this, !arcoreInstallRequested)
            when (installStatus) {
                ArCoreApk.InstallStatus.INSTALLED -> {
                    // ARCore is installed and up to date. Activate it.
                    vulkanSurfaceView.activateArcore(this, this)
                }
                ArCoreApk.InstallStatus.INSTALL_REQUESTED -> {
                    // ARCore has prompted the user to install/update.
                    // The activity will be paused and resumed when done.
                    arcoreInstallRequested = true
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "ARCore installation failed", e)
            Toast.makeText(
                this,
                "Failed to set up AR: ${e.message}",
                Toast.LENGTH_LONG
            ).show()
            finish()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
    }
}
