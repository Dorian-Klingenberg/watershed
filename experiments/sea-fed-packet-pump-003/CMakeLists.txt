add_executable(sea_fed_packet_pump_003_experiment
    main.cpp
    simulator.cpp
    dashboard.cpp
)

target_include_directories(sea_fed_packet_pump_003_experiment
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set_property(TARGET sea_fed_packet_pump_003_experiment PROPERTY FOLDER "Experiments")
