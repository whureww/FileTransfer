plugins {
    alias(libs.plugins.androidApplication)
    alias(libs.plugins.kotlinMultiplatform)
    alias(libs.plugins.composeMultiplatform)
    alias(libs.plugins.composeCompiler)
}

kotlin {
    androidTarget {
        compilations.all {
            kotlinOptions {
                jvmTarget = "17"
            }
        }
    }
    
    sourceSets {
        androidMain.dependencies {
            implementation(project(":shared"))
            implementation(compose.runtime)
            implementation(compose.foundation)
            implementation(compose.material3)
            implementation(compose.ui)
            implementation(compose.components.resources)
            implementation("androidx.activity:activity-compose:1.9.1")
            implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.4")
        }
    }
}

android {
    namespace = "com.filetransfer"
    compileSdk = 34
    
    defaultConfig {
        applicationId = "com.filetransfer"
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "0.0.7"
    }
    
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    
    buildFeatures {
        compose = true
    }
}
