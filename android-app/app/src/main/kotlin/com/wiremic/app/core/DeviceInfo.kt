package com.wiremic.app.core

import org.json.JSONArray
import org.json.JSONObject

data class DeviceInfo(
    val id: String,
    val name: String,
    val model: String,
    val platform: String,
    val ip: String,
    val connectionType: String,
    val controlPort: Int,
    val status: String = ""
) {
    companion object {
        fun fromJson(json: JSONObject): DeviceInfo = DeviceInfo(
            id = json.optString("id"),
            name = json.optString("name"),
            model = json.optString("model"),
            platform = json.optString("platform"),
            ip = json.optString("ip"),
            connectionType = json.optString("connectionType"),
            controlPort = json.optInt("controlPort"),
            status = json.optString("status")
        )

        fun listFromJson(jsonArray: String): List<DeviceInfo> {
            return try {
                val array = JSONArray(jsonArray)
                (0 until array.length()).map { fromJson(array.getJSONObject(it)) }
            } catch (e: Exception) {
                emptyList()
            }
        }
    }
}
