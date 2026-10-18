# Live demo notes

## Before they arrive

1. From the repository root, start the browser workbench:

   ```sh
   dotnet run --project projects/grannys-house-trials/webgpu-fluid-sandbox/WebGpuFluidSandbox.csproj
   ```

2. Open the local address printed by the app (the default is
   `http://localhost:5119`) in Chrome.
3. Check that the water-flow view appears. The workbench is a separate
   experiment; it is not the Granny's Yard game prototype.
4. Leave the browser ready on the **Incline → flat** terrain preset if it is
   available, with the app paused and the controls visible.

## Show them

Keep the demo to a couple of minutes:

1. Point out the slope and the flatter land below it.
2. Add a small pour of water near the higher side.
3. Step or run the simulation and watch where water gathers or travels.
4. If useful, make one small terrain change and try again.

Say: “This is a workbench for testing how water behaves. It helps us explore
ideas for the game, but it isn’t the game itself.”

Avoid explaining algorithms, code, or performance numbers unless they ask.

## If the live demo does not cooperate

Show [`images/water-flow-demo-fallback.png`](./images/water-flow-demo-fallback.png)
and say: “This is a sample terrain setup from the same experiment; the live
version lets me add water and change the land.”

The browser workbench can still run its C# water model when WebGPU is
unavailable, but the live display may differ. Do not spend the visit debugging
the demo.
