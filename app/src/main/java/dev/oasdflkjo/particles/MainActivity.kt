package dev.oasdflkjo.particles

import android.app.AlertDialog
import android.app.NativeActivity
import android.content.Intent
import android.os.Bundle
import android.widget.TextView
import android.view.View
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import com.google.androidgamesdk.GameActivity

class MainActivity : GameActivity() {
    companion object {
        private const val DEBUG_FORCE_SHOW_PRIVACY = false  // Disabled for release
        init {
            System.loadLibrary("particles")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // For debugging: Clear preferences if debug flag is set
        if (DEBUG_FORCE_SHOW_PRIVACY) {
            getSharedPreferences("app_prefs", MODE_PRIVATE)
                .edit()
                .clear()
                .apply()
        }
        
        // Check if we need to show the privacy policy
        val prefs = getSharedPreferences("app_prefs", MODE_PRIVATE)
        val privacyShown = prefs.getBoolean("privacy_shown", false)
        
        if (!privacyShown) {
            showPrivacyDialog()
        }
        // Note: We don't call launchNativeActivity() here because GameActivity is already our native activity
    }
    
    private fun showPrivacyDialog() {
        // Create TextView programmatically with better styling
        val textView = TextView(this).apply {
            setPadding(48, 32, 48, 32)
            textSize = 16f
            setTextColor(Color.WHITE)
            setLineSpacing(0f, 1.2f)  // Add some line spacing
        }
        
        // Load and set privacy policy text
        try {
            val policy = assets.open("privacy_policy.txt").bufferedReader().use { it.readText() }
            textView.text = policy
        } catch (e: Exception) {
            textView.text = "Privacy Policy: This app collects no data."
        }
        
        // Create and style the dialog
        val dialog = AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog)
            .setTitle("Privacy Policy")
            .setView(textView)
            .setPositiveButton("Cool! 👍") { dialog, _ ->
                dialog.dismiss()
                // Save that we've shown the policy
                getSharedPreferences("app_prefs", MODE_PRIVATE)
                    .edit()
                    .putBoolean("privacy_shown", true)
                    .apply()
            }
            .setCancelable(false)
            .create()

        // Set the dialog background to our custom drawable
        dialog.window?.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
        dialog.window?.setBackgroundDrawableResource(R.drawable.dialog_background)
        
        // Show the dialog
        dialog.show()
        
        // Style the title and button
        dialog.getButton(AlertDialog.BUTTON_POSITIVE)?.apply {
            setTextColor(Color.parseColor("#BB86FC"))  // Material Design purple
            textSize = 16f
            setPadding(48, 24, 48, 24)
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemUi()
        }
    }

    private fun hideSystemUi() {
        val decorView = window.decorView
        decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_FULLSCREEN)
    }
}