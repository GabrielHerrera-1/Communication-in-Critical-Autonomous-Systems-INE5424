#include "../src/application/component_ports.h"
#include "load_stress_scenario.h"

int main() {
    const load_stress_scenario::Config config = {
        "load-stress",
        8,
        10,
        15,
        Component_Ports::TEST_LOAD_STRESS,
    };

    return load_stress_scenario::run(config);
}
