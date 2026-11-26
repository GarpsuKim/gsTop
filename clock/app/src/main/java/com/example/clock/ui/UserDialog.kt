package com.example.clock.ui

import android.Manifest
import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.provider.ContactsContract
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContract
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Contacts
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.example.clock.R
import com.example.clock.UserInfo
import com.example.clock.UserStore
import com.example.clock.readPickedPhone

/** [👤 사용자 등록] 버튼: 누르면 사용자 등록 창. */
@Composable
fun UserButton(onClick: () -> Unit) {
    TextButton(
        onClick = onClick,
        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
    ) {
        Icon(Icons.Filled.Person, contentDescription = null, modifier = Modifier.size(16.dp))
        Spacer(Modifier.width(4.dp))
        Text(stringResource(R.string.user_register), style = MaterialTheme.typography.labelMedium)
    }
}

/** 시스템 연락처 앱에서 "전화번호 하나" 를 고름. 고른 한 건만 읽을 수 있으므로 연락처 권한이 필요 없음. */
private class PickPhoneNumber : ActivityResultContract<Unit, Uri?>() {
    override fun createIntent(context: Context, input: Unit) =
        Intent(Intent.ACTION_PICK, ContactsContract.CommonDataKinds.Phone.CONTENT_URI)

    override fun parseResult(resultCode: Int, intent: Intent?): Uri? =
        if (resultCode == Activity.RESULT_OK) intent?.data else null
}

/**
 * 사용자 등록 창: 사용자 성명 + 긴급 연락처 (연락처 앱에서 선택).
 * 긴급 연락처를 고르면 위젯 두 번 누르기로 바로 전화할 수 있도록 전화 권한도 요청.
 */
@Composable
fun UserRegisterDialog(onDismiss: () -> Unit) {
    val context = LocalContext.current
    val saved = remember { UserStore.load(context) }
    var name by rememberSaveable { mutableStateOf(saved.name) }
    var emergencyName by rememberSaveable { mutableStateOf(saved.emergencyName) }
    var emergencyNumber by rememberSaveable { mutableStateOf(saved.emergencyNumber) }

    // 결과와 상관없이 진행 (거부하면 전화 앱에 번호만 띄워 줌)
    val callPermission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { }
    val pickContact = rememberLauncherForActivityResult(PickPhoneNumber()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult // 고르지 않고 돌아옴
        val picked = readPickedPhone(context, uri)
        if (picked == null) {
            Toast.makeText(context, R.string.user_emergency_failed, Toast.LENGTH_SHORT).show()
            return@rememberLauncherForActivityResult
        }
        emergencyName = picked.first
        emergencyNumber = picked.second
        val granted = ContextCompat.checkSelfPermission(context, Manifest.permission.CALL_PHONE) ==
            PackageManager.PERMISSION_GRANTED
        if (!granted) callPermission.launch(Manifest.permission.CALL_PHONE)
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.user_title)) },
        text = {
            Column {
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it },
                    label = { Text(stringResource(R.string.user_name)) },
                    singleLine = true,
                )
                Spacer(Modifier.height(16.dp))
                Text(stringResource(R.string.user_emergency), style = MaterialTheme.typography.labelLarge)
                Spacer(Modifier.height(4.dp))
                Text(
                    if (emergencyNumber.isBlank()) {
                        stringResource(R.string.user_emergency_none)
                    } else {
                        "$emergencyName  $emergencyNumber".trim()
                    },
                    style = MaterialTheme.typography.bodyLarge,
                )
                TextButton(onClick = {
                    try {
                        pickContact.launch(Unit)
                    } catch (e: ActivityNotFoundException) {
                        Toast.makeText(context, R.string.user_contacts_not_found, Toast.LENGTH_SHORT).show()
                    }
                }) {
                    Icon(Icons.Filled.Contacts, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(6.dp))
                    Text(stringResource(R.string.user_emergency_pick))
                }
            }
        },
        confirmButton = {
            TextButton(
                enabled = name.isNotBlank(), // 성명은 꼭 입력
                onClick = {
                    UserStore.save(
                        context,
                        UserInfo(name.trim(), emergencyName.trim(), emergencyNumber.trim()),
                    )
                    onDismiss()
                },
            ) { Text(stringResource(R.string.user_save)) }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text(stringResource(R.string.cancel)) }
        },
    )
}
