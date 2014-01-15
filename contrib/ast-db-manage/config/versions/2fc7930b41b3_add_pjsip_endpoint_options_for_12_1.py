"""Add pjsip endpoint options for 12.1

Revision ID: 2fc7930b41b3
Revises: 581a4264e537
Create Date: 2014-01-14 09:23:53.923454

"""

# revision identifiers, used by Alembic.
revision = '2fc7930b41b3'
down_revision = '581a4264e537'

from alembic import op
import sqlalchemy as sa

YESNO_VALUES = ['yes', 'no']
REDIRECT_METHODS = ['user', 'uri_core', 'uri_pjsip']

def upgrade():
    op.add_column('ps_endpoints', sa.Column('redirect_method', sa.Enum(*REDIRECT_METHODS, name='redirect_methods')))
    op.add_column('ps_endpoints', sa.Column('set_var', sa.Text()))
    op.add_column('ps_contacts', sa.Column('path', sa.Text()))
    op.add_column('ps_aors', sa.Column('support_path', sa.Enum(*YESNO_VALUES, name='yesno_values')))
    op.add_column('ps_registrations', sa.Column('support_path', sa.Enum(*YESNO_VALUES, name='yesno_values')))


def downgrade():
    op.drop_column('ps_endpoints', 'redirect_method')
    op.drop_column('ps_endpoints', 'set_var')
    op.drop_column('ps_contacts', 'path')
    op.drop_column('ps_aors', 'support_path')
    op.drop_column('ps_registrations', 'support_path')
