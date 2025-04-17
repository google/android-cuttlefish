import {Component, inject} from '@angular/core';
import {DeviceService} from '../device.service';
import {DisplaysService} from '../displays.service';
import {filter, first, mergeMap} from 'rxjs/operators';
import {GroupService} from '../group.service';
import {ActivatedRoute, NavigationEnd, Router} from '@angular/router';

@Component({
  standalone: false,
  selector: 'app-device-pane',
  templateUrl: './device-pane.component.html',
  styleUrls: ['./device-pane.component.scss'],
})
export class DevicePaneComponent {
  private deviceService = inject(DeviceService);
  displaysService = inject(DisplaysService);
  private groupService = inject(GroupService);
  private activatedRoute = inject(ActivatedRoute);
  private router = inject(Router);

  groups = this.groupService.getGroups();
  devices = this.deviceService.getDevices();

  ngOnInit(): void {
    this.router.events
      .pipe(
        filter(event => event instanceof NavigationEnd),
        mergeMap(() => this.activatedRoute.queryParams)
      )
      .subscribe(params =>
        this.deviceService.setGroupId(params['groupId'] ?? null)
      );

    this.deviceService.refresh();
    this.groupService.refresh();
  }

  onRefresh(): void {
    this.deviceService.refresh();
    this.groupService.refresh();
  }

  showAll(): void {
    this.devices.pipe(first()).subscribe(devices => {
      devices.forEach(device => {
        if (!this.displaysService.isVisibleDevice(device.device_id)) {
          this.displaysService.toggleVisibility(device.device_id);
        }
      });
    });
  }
}
