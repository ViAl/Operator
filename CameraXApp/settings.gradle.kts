pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "CameraXApp"
include(":app")
include(":core:common")
include(":core:model")
include(":core:camera")
include(":core:pipeline")
include(":core:native")
include(":feature:viewfinder")
