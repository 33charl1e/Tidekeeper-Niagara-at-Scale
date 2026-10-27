# Physical playtest checklist

The arena is created by C++ when Play begins. Start with the default 500 CPU
agents, then press Tab for 100,000 GPU agents. The automated route supports demos
and reproducible measurements; it does not replace a human review of the controls.

- Confirm WASD and held left mouse both move the yellow beacon in the expected direction.
- Hold right mouse and confirm nearby agents follow; release it and test the repel pulse.
- Guide the school into the sanctuary and check that rescued agents match the score.
- Press R and check the score resets. Test Tab and +/- for fresh rounds and correct counts.
- Press P and check positions and the timer freeze, then resume correctly.
- Check the normal 70% win condition and the normal timeout loss condition.
- Compare readability and control at similar frame times. Record one concrete reason for your preference.

Keep human acceptance separate from automation results. The supplied measurements
come from the development laptop. Run Scripts/RunBenchmark.ps1 on the required
lab machine to establish performance on that hardware.

Personally view the official videos for both games in Docs/ReferenceNotes.md and
add observations from your viewing or play session.
