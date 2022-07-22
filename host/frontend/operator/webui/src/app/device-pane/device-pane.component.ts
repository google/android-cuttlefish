import { Component, OnInit } from "@angular/core";
import { Device } from "../device-interface";
import { DeviceService } from "../device.service";
import { DisplaysService } from "../displays.service";
import { Observable, of } from "rxjs";

@Component({
  selector: "app-device-pane",
  templateUrl: "./device-pane.component.html",
  styleUrls: ["./device-pane.component.sass"],
})
export class DevicePaneComponent implements OnInit {
  devices: Observable<Device[]> = of([]);
  deviceTemp: Device[] = [];
  selectedDevice?: Device;

  constructor(
    private deviceService: DeviceService,
    private displaysService: DisplaysService
  ) {}

  ngOnInit(): void {
    this.getDevices();
  }

  onSelect(device: Device): void {
    device.isVisible = !device.isVisible;
    this.selectedDevice = device;
    if (this.selectedDevice.isVisible == false) {
      this.displaysService.remove(`${this.selectedDevice.id}`);
    } else {
      this.displaysService.add(`${this.selectedDevice.id}`);
    }
  }

  //Needs to be fixed.
  onRefresh(): void {
    this.getDevices();
  }

  getDevices(): void {
    this.deviceService.getDevices().subscribe((info) => {
      info.forEach((id) => {
        this.deviceTemp.push({ id: id, isVisible: false });
      });
      this.devices = of(this.deviceTemp);
    });

    this.deviceTemp = [];
  }
}
