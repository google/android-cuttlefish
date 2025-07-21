import {Component, inject} from '@angular/core';
import {DeviceService} from '../device.service';
import {DisplaysService} from '../displays.service';
import {filter, first, mergeMap} from 'rxjs/operators';
import {ActivatedRoute, NavigationEnd, Router} from '@angular/router';
import {DeviceItem} from '../device-item.interface';

@Component({
  standalone: false,
  selector: 'app-device-pane',
  templateUrl: './device-pane.component.html',
  styleUrls: ['./device-pane.component.scss'],
})
export class DevicePaneComponent {
  private deviceService = inject(DeviceService);
  displaysService = inject(DisplaysService);
  private activatedRoute = inject(ActivatedRoute);
  private router = inject(Router);

  legacyDevices = this.deviceService.getLegacyDevices();
  deviceGroups = this.deviceService.getDeviceGroups();

  ngOnInit(): void {
    this.router.events
        .pipe(
            filter(event => event instanceof NavigationEnd),
            mergeMap(() => this.activatedRoute.queryParams))
        .subscribe(params => this.deviceService.setDeviceFilter({
          owner: params['owner'] ?? null,
          groupId: params['groupId'] ?? null
        }));

    this.deviceService.refresh();
  }

  onRefresh(): void {
    this.deviceService.refresh();
  }

  showAll(): void {
    let show = (device: DeviceItem) => {
      console.log(device);
      if (!this.displaysService.isVisibleDevice(device.device_id)) {
        this.displaysService.toggleVisibility(device.device_id);
      }
    };
    this.deviceGroups.pipe(first()).subscribe(groups => {
      for (const group of groups) {
        group.devices.forEach(show);
      }
    });
    this.legacyDevices.pipe(first()).subscribe(devices => {
      devices.forEach(show);
    });
  }
}
