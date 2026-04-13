#include "../src/application/component_ports.h"
#include "load_stress_scenario.h"

int main() {
    const load_stress_scenario::Config config = {
        "burst-stress",
        16,
        16,
        15,
        Component_Ports::TEST_BURST_STRESS,
    };

    return load_stress_scenario::run(config);
}
