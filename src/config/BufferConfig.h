#pragma once

// ============================================================================
// FIREBASE BUFFER SIZES
// ============================================================================

// Main Firebase Operations
#define FIREBASE_RESPONSE_SIZE 8192
#define FIREBASE_SSL_BUFFER_SIZE 8191
#define FIREBASE_SSL_RECEIVE_BUFFER 2048

// Firebase Stream Operations
#define STREAM_RESPONSE_SIZE 2048
#define STREAM_SSL_BUFFER_SIZE 4096
#define STREAM_SSL_RECEIVE_BUFFER 1024

// Command Deletion Queue
#define MAX_DELETE_QUEUE 10
