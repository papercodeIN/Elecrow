package com.fusionautomate.aanav

import org.json.JSONObject

/**
 * Parses the Google Maps turn-by-turn notification text into the display's
 * protocol v1 JSON line (see the project README for the schema).
 *
 * The Maps notification is unstructured and varies across Maps versions and
 * locales; this parser is regex-based with graceful fallbacks:
 *  - no distance found  -> update is dropped (not a nav notification),
 *  - no street extracted -> street is left empty (display shows distance only),
 *  - unknown maneuver    -> "ST" (straight), never a blank card.
 */
object NavParser {

    data class Update(
        val maneuver: String,
        val distMeters: Int,
        val distText: String,
        val street: String,
        val thenStreet: String,
        val state: String,
        val rawText: String
    ) {
        fun toJson(): String {
            val o = JSONObject()
            o.put("t", "nav")
            o.put("m", maneuver)
            o.put("d", distMeters)
            o.put("ds", distText)
            o.put("st", street)
            o.put("th", thenStreet)
            o.put("eta", JSONObject.NULL)
            o.put("rt", JSONObject.NULL)
            o.put("v", 0)
            o.put("s", state)
            return o.toString() + "\n"
        }
    }

    private val distRegex = Regex(
        pattern = "(?:in\\s+)?([0-9]+(?:[.,][0-9]+)?)\\s*(m|km|ft|mi)\\b",
        options = setOf(RegexOption.IGNORE_CASE)
    )

    private val thenRegex = Regex(
        pattern = "then(?:\\s+in\\s+[0-9.,]+\\s*(?:m|km|ft|mi))?[\\s:]*([^\\n]{2,40})",
        options = setOf(RegexOption.IGNORE_CASE)
    )

    private val streetRegex = Regex(
        pattern = "(?:onto|to|toward|towards|exit|at)\\s+(.+?)" +
                "(?=\\s+then\\b|\\s+in\\s+[0-9]|[,.;]|$)",
        options = setOf(RegexOption.IGNORE_CASE)
    )

    fun parse(title: String?, text: String?, bigText: String?): Update? {
        val body = listOfNotNull(bigText, text, title).firstOrNull { !it.isNullOrBlank() }
            ?: return null
        val combined = listOfNotNull(title, body).joinToString(" ").trim()

        val state = when {
            combined.contains("rerout", ignoreCase = true) ||
                combined.contains("finding", ignoreCase = true) -> "reroute"
            combined.contains("arrived", ignoreCase = true) ||
                combined.contains("destination", ignoreCase = true) &&
                distRegex.find(combined) == null -> "arrived"
            else -> "nav"
        }

        val dm = distRegex.find(combined) ?: return null
        val value = dm.groupValues[1].replace(',', '.').toDoubleOrNull() ?: return null
        val unit = dm.groupValues[2].lowercase()
        val meters = when (unit) {
            "km" -> (value * 1000).toInt()
            "mi" -> (value * 1609.34).toInt()
            "ft" -> (value * 0.3048).toInt()
            else -> value.toInt()
        }

        return Update(
            maneuver = detectManeuver(combined),
            distMeters = meters,
            distText = fmtDistance(value, unit),
            street = streetRegex.find(combined)?.groupValues?.get(1)?.trim()
                ?.take(50) ?: "",
            thenStreet = thenRegex.find(combined)?.groupValues?.get(1)?.trim()
                ?.take(40) ?: "",
            state = state,
            rawText = combined.take(120)
        )
    }

    private fun detectManeuver(s: String): String {
        val t = s.lowercase()
        return when {
            t.contains("u-turn") || t.contains("uturn") -> "UT"
            t.contains("roundabout") -> "RAB"
            t.contains("sharp left") -> "SHL"
            t.contains("sharp right") -> "SHR"
            t.contains("slight left") -> "SL"
            t.contains("slight right") -> "SR"
            t.contains("turn left") || t.contains("left turn") -> "TL"
            t.contains("turn right") || t.contains("right turn") -> "TR"
            t.contains("exit left") -> "EXL"
            t.contains("exit right") -> "EXR"
            t.contains("merge left") || t.contains("keep left") -> "MEL"
            t.contains("merge right") || t.contains("keep right") -> "MER"
            t.contains("fork left") -> "FKL"
            t.contains("fork right") -> "FKR"
            t.contains("arrive") || t.contains("destination") -> "ARR"
            t.contains("left") -> "TL"
            t.contains("right") -> "TR"
            else -> "ST"
        }
    }

    private fun fmtDistance(value: Double, unit: String): String {
        val num = if (value == value.toInt().toDouble()) {
            value.toInt().toString()
        } else {
            String.format(java.util.Locale.US, "%.1f", value)
        }
        return "$num $unit"
    }
}
