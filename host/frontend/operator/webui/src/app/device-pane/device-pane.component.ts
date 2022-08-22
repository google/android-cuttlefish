import { Component, OnInit } from "@angular/core";
import { Device } from "../device-interface";
import { DeviceService } from "../device.service";
import { DisplaysService } from "../displays.service";
import { Observable, of, map, BehaviorSubject } from "rxjs";

@Component({
  selector: "app-device-pane",
  templateUrl: "./device-pane.component.html",
  styleUrls: ["./device-pane.component.sass"],
})
export class DevicePaneComponent implements OnInit {
  devices = new BehaviorSubject<Device[]>([]);
  private currentDevices: Device[] = [];

  constructor(
    private deviceService: DeviceService,
    public displaysService: DisplaysService
  ) {}

  ngOnInit(): void {
    this.deviceService.getDevices().subscribe((devices) => {
      this.devices.next(devices);
    });
  }

  trackById(index : number, device : Device) {
    return device.deviceId;
  }

  onSelect(device: Device): void {
    if (this.displaysService.visibleValidate(device)) {
      this.displaysService.remove(device);
    } else {
      this.displaysService.add(device);
    }
  }

  onRefresh(): void {
    this.deviceService.refresh();
    this.displaysService.refresh();
  }

  showAll(): void {
    this.devices.subscribe((devices) => {
      this.currentDevices = devices;
    })
    this.currentDevices.forEach((device) => {
      if(!this.displaysService.visibleValidate(device)) {
        this.onSelect(device);
      }
    });
  }
}
