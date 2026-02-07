package dev.geronimodesenvolvimentos.krakatoa

import android.Manifest
import android.content.pm.PackageManager
import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.widget.TextView
import dev.geronimodesenvolvimentos.krakatoa.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {
    private lateinit var vulkanSurfaceView: VulkanSurfaceView
    private var cameraPermissionGranted = false;
    fun isCameraPermissionGranted() = cameraPermissionGranted

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        vulkanSurfaceView = VulkanSurfaceView(this, assets)
        setContentView(vulkanSurfaceView)
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.CAMERA), 1001)
        } else {
            cameraPermissionGranted = true
        }
    }
    override fun onResume() {
        super.onResume()
        vulkanSurfaceView.onResume()
    }

    override fun onPause() {
        vulkanSurfaceView.onPause()
        super.onPause()
    }
    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<String>, results: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, results)
        if (requestCode == 1001 && results.isNotEmpty() && results[0] == PackageManager.PERMISSION_GRANTED) {
            cameraPermissionGranted = true
            vulkanSurfaceView.activateArcore(this, this)
        }
    }

    override fun onDestroy() {
        vulkanSurfaceView.cleanup()
        super.onDestroy()
    }


}