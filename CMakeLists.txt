option( PLUGIN_STANDARD_QXRAYPLANVIEW "Install qXRayPlanView plugin" OFF )

if ( PLUGIN_STANDARD_QXRAYPLANVIEW )
	project( QXRAYPLANVIEW_PLUGIN )

	AddPlugin( NAME ${PROJECT_NAME} )

	add_subdirectory( include )
	add_subdirectory( src )
endif()

