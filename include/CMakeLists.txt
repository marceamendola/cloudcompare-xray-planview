target_sources( ${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_LIST_DIR}/qXRayPlanView.h
		${CMAKE_CURRENT_LIST_DIR}/XRayDialog.h
		${CMAKE_CURRENT_LIST_DIR}/XRayOverlayLayer.h
		${CMAKE_CURRENT_LIST_DIR}/NormalsOverlayLayer.h
		${CMAKE_CURRENT_LIST_DIR}/XRayViewport.h
)

target_include_directories( ${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
)
