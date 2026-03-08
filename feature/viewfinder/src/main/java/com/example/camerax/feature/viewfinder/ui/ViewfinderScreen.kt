package com.example.camerax.feature.viewfinder.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.example.camerax.core.model.pipeline.CameraSessionState
import com.example.camerax.core.model.pipeline.CapturePipelineState
import com.example.camerax.feature.viewfinder.viewmodel.ViewfinderViewModel

@Composable
fun ViewfinderScreen(
    viewModel: ViewfinderViewModel = hiltViewModel()
) {
    val lifecycleOwner = LocalLifecycleOwner.current
    val sessionState by viewModel.sessionState.collectAsState()
    val pipelineState by viewModel.captureState.collectAsState()
    val lastSavedUri by viewModel.lastSavedUri.collectAsState()

    // Unbind gracefully ONLY on dispose.
    // ProcessCameraProvider handles ON_STOP / ON_START resume states automatically!
    DisposableEffect(lifecycleOwner) {
        onDispose {
            viewModel.unbindCamera()
        }
    }

    Box(modifier = Modifier.fillMaxSize().background(Color.Black)) {
        // Preview Background
        CameraPreview(
            modifier = Modifier.fillMaxSize(),
            onPreviewCreated = { previewView ->
                // Bind only once when AndroidView is created
                viewModel.bindCamera(lifecycleOwner, previewView)
            }
        )

        // Overlay status string
        Column(
            modifier = Modifier
                .align(Alignment.TopCenter)
                .padding(16.dp)
                .background(Color.Black.copy(alpha = 0.5f), shape = MaterialTheme.shapes.small)
                .padding(8.dp)
        ) {
            Text("Session: ${sessionState.javaClass.simpleName}", color = Color.White)
            Text("Pipeline: ${pipelineState.javaClass.simpleName}", color = Color.White)
            lastSavedUri?.let { Text("Saved: $it", color = Color.Green, maxLines = 1) }
        }

        // Shutter button
        val isCapturing = pipelineState is CapturePipelineState.AcquiringFrames ||
                pipelineState is CapturePipelineState.Processing ||
                pipelineState is CapturePipelineState.Saving

        Button(
            onClick = { viewModel.onShutterClicked() },
            enabled = sessionState is CameraSessionState.Active && !isCapturing,
            shape = CircleShape,
            colors = ButtonDefaults.buttonColors(
                containerColor = Color.White,
                disabledContainerColor = Color.Gray
            ),
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(bottom = 32.dp)
                .size(72.dp)
        ) {
            // Keep empty for circular look
        }
        
        if (isCapturing) {
            CircularProgressIndicator(
                modifier = Modifier.align(Alignment.Center),
                color = Color.White
            )
        }
    }
}
