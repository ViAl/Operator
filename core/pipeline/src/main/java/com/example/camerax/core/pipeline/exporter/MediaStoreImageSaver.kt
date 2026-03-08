package com.example.camerax.core.pipeline.exporter

import android.content.ContentValues
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.ImageFormat
import android.graphics.Matrix
import android.graphics.Rect
import android.graphics.YuvImage
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import com.example.camerax.core.common.dispatchers.AppDispatchers
import com.example.camerax.core.common.logger.Logger
import com.example.camerax.core.model.pipeline.NativeProcessResult
import com.example.camerax.core.model.pipeline.SaveResult
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream
import java.io.OutputStream
import javax.inject.Inject

class MediaStoreImageSaver @Inject constructor(
    @ApplicationContext private val context: Context,
    private val dispatchers: AppDispatchers,
    private val logger: Logger
) : ImageSaver {

    override suspend fun save(result: NativeProcessResult): SaveResult = withContext(dispatchers.io) {
        logger.d("ImageSaver", "Saving image result...")
        try {

            val width: Int
            val height: Int
            val nv21: ByteArray
            val rotationDegrees: Int
            when (result) {
                is NativeProcessResult.Success -> {
                    width = result.w
                    height = result.h
                    nv21 = result.processedData
                    rotationDegrees = result.rotationDegrees
                }
                is NativeProcessResult.Fallback -> {
                    val frame = result.baseFrame
                    width = frame.width
                    height = frame.height
                    nv21 = frame.nv21Data
                    rotationDegrees = frame.metadata.rotationDegrees
                }
                is NativeProcessResult.Error -> {
                    throw Exception("Cannot save an error result: ${result.reason}")
                }
            }

            // Convert NV21 byte array to JPEG
            val yuvImage = YuvImage(nv21, ImageFormat.NV21, width, height, null)
            val outStream = ByteArrayOutputStream()
            yuvImage.compressToJpeg(Rect(0, 0, width, height), 95, outStream)
            var jpegBytes = outStream.toByteArray()

            if (rotationDegrees != 0) {
                val bitmap = BitmapFactory.decodeByteArray(jpegBytes, 0, jpegBytes.size)
                val matrix = Matrix().apply {
                    postRotate(rotationDegrees.toFloat())
                }
                val rotatedBitmap = Bitmap.createBitmap(bitmap, 0, 0, bitmap.width, bitmap.height, matrix, true)
                val rotatedStream = ByteArrayOutputStream()
                rotatedBitmap.compress(Bitmap.CompressFormat.JPEG, 95, rotatedStream)
                jpegBytes = rotatedStream.toByteArray()
                bitmap.recycle()
                rotatedBitmap.recycle()
            }

            val filename = "MVP_IMG_${System.currentTimeMillis()}.jpg"
            val contentValues = ContentValues().apply {
                put(MediaStore.MediaColumns.DISPLAY_NAME, filename)
                put(MediaStore.MediaColumns.MIME_TYPE, "image/jpeg")
                // Protect against API 26-28 crashes where RELATIVE_PATH does not exist
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_PICTURES)
                }
            }

            val uri = context.contentResolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, contentValues)
                ?: throw Exception("Failed to create MediaStore entry")

            context.contentResolver.openOutputStream(uri)?.use { os: OutputStream ->
                os.write(jpegBytes)
            } ?: throw Exception("Failed to open output stream for URI")

            logger.d("ImageSaver", "Saved successfully to $uri")
            SaveResult.Success(uri.toString())
        } catch (e: Exception) {
            logger.e("ImageSaver", "Save failed", e)
            SaveResult.Error(e)
        }
    }

    override suspend fun saveJpeg(jpegBytes: ByteArray): SaveResult = withContext(dispatchers.io) {
        logger.d("ImageSaver", "Saving full-res JPEG (${jpegBytes.size} bytes)...")
        try {
            val filename = "IMG_${System.currentTimeMillis()}.jpg"
            val contentValues = ContentValues().apply {
                put(MediaStore.MediaColumns.DISPLAY_NAME, filename)
                put(MediaStore.MediaColumns.MIME_TYPE, "image/jpeg")
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_PICTURES)
                }
            }

            val uri = context.contentResolver.insert(
                MediaStore.Images.Media.EXTERNAL_CONTENT_URI, contentValues
            ) ?: throw Exception("Failed to create MediaStore entry")

            context.contentResolver.openOutputStream(uri)?.use { os: OutputStream ->
                os.write(jpegBytes)
            } ?: throw Exception("Failed to open output stream for URI")

            logger.d("ImageSaver", "Full-res JPEG saved to $uri")
            SaveResult.Success(uri.toString())
        } catch (e: Exception) {
            logger.e("ImageSaver", "Full-res JPEG save failed", e)
            SaveResult.Error(e)
        }
    }
}
