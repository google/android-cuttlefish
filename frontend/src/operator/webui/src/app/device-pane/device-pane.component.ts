import {Component} from '@angular/core';
import {DeviceService} from '../device.service';
import {DisplaysService} from '../displays.service';
import {filter, first, mergeMap} from 'rxjs/operators';
import {GroupService} from '../group.service';
import {ActivatedRoute, NavigationEnd, Router} from '@angular/router';
import {MatSnackBar} from '@angular/material/snack-bar'

@Component({
  selector: 'app-device-pane',
  templateUrl: './device-pane.component.html',
  styleUrls: ['./device-pane.component.scss'],
})
export class DevicePaneComponent {
  groups = this.groupService.getGroups();
  devices = this.deviceService.getDevices();

  constructor(
    private deviceService: DeviceService,
    public displaysService: DisplaysService,
    private groupService: GroupService,
    private activatedRoute: ActivatedRoute,
    private router: Router,
    private snackBar: MatSnackBar
  ) {}

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

  copyAdbCommand(deviceId: string) {
    var currentUrl = window.location.href
    var command = 'cvdr connect --connect_agent=websocket_agent ' +
                  `--service_url=${currentUrl} --host=websocket ${deviceId}`
    navigator.clipboard.writeText(command)
    this.snackBar.open('ADB connect command copied to the Clipboard', 'OK', {
      duration: 3000,
    });
  }
}
