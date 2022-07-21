import { Component, OnInit } from "@angular/core";
import { Device } from "../device-interface";
import { DEVICES } from "../device-mocks";

@Component({
  selector: "app-device-pane",
  templateUrl: "./device-pane.component.html",
  styleUrls: ["./device-pane.component.sass"],
})
export class DevicePaneComponent implements OnInit {
  //This is temporary variable.
  //It will be fixed to a function (create device list function) after connecting to services.
  devices = DEVICES;
  selectedDevice?: Device;

  constructor() {}

  ngOnInit(): void {}

  onSelect(device: Device): void {
    this.selectedDevice = device;
  }

  //Temporary function. Also will be updated after connecting to service.
  //console log to check out whether refresh button works properly
  //Unchecking checkboxes will be implemented after update.
  onRefresh(): void {
    DEVICES.push({ id: "another one" });
    this.devices = DEVICES;
  }
}
