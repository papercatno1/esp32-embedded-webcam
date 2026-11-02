package com.papercatno1.notepad

import android.content.Intent
import android.os.Bundle
import android.widget.Button
import android.widget.Toast
import androidx.activity.ComponentActivity

/** 主入口：功能菜单。以后新功能就在这加一项。 */
class MenuActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_menu)

        findViewById<Button>(R.id.btnSys).setOnClickListener {
            startActivity(Intent(this, SysActivity::class.java))
        }
        findViewById<Button>(R.id.btnCam).setOnClickListener {
            startActivity(Intent(this, WebCamActivity::class.java))
        }
        findViewById<Button>(R.id.btnNote).setOnClickListener {
            startActivity(Intent(this, MainActivity::class.java))
        }
    }
}
